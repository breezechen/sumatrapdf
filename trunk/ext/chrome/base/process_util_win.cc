// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process_util.h"

#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <userenv.h>
#include <psapi.h>

#include <ios>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/debug/stack_trace.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop.h"
#include "base/metrics/histogram.h"
#include "base/win/object_watcher.h"
#include "base/win/scoped_handle.h"
#include "base/win/windows_version.h"

// userenv.dll is required for CreateEnvironmentBlock().
#pragma comment(lib, "userenv.lib")

namespace base {

namespace {

// System pagesize. This value remains constant on x86/64 architectures.
const int PAGESIZE_KB = 4;

// Exit codes with special meanings on Windows.
const DWORD kNormalTerminationExitCode = 0;
const DWORD kDebuggerInactiveExitCode = 0xC0000354;
const DWORD kKeyboardInterruptExitCode = 0xC000013A;
const DWORD kDebuggerTerminatedExitCode = 0x40010004;

// Maximum amount of time (in milliseconds) to wait for the process to exit.
static const int kWaitInterval = 2000;

// This exit code is used by the Windows task manager when it kills a
// process.  It's value is obviously not that unique, and it's
// surprising to me that the task manager uses this value, but it
// seems to be common practice on Windows to test for it as an
// indication that the task manager has killed something if the
// process goes away.
const DWORD kProcessKilledExitCode = 1;

// HeapSetInformation function pointer.
typedef BOOL (WINAPI* HeapSetFn)(HANDLE, HEAP_INFORMATION_CLASS, PVOID, SIZE_T);

// Previous unhandled filter. Will be called if not NULL when we intercept an
// exception. Only used in unit tests.
LPTOP_LEVEL_EXCEPTION_FILTER g_previous_filter = NULL;

// Prints the exception call stack.
// This is the unit tests exception filter.
long WINAPI StackDumpExceptionFilter(EXCEPTION_POINTERS* info) {
  debug::StackTrace(info).PrintBacktrace();
  if (g_previous_filter)
    return g_previous_filter(info);
  return EXCEPTION_CONTINUE_SEARCH;
}

// Connects back to a console if available.
void AttachToConsole() {
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
    unsigned int result = GetLastError();
    // Was probably already attached.
    if (result == ERROR_ACCESS_DENIED)
      return;

    if (result == ERROR_INVALID_HANDLE || result == ERROR_INVALID_HANDLE) {
      // TODO(maruel): Walk up the process chain if deemed necessary.
    }
    // Continue even if the function call fails.
    AllocConsole();
  }
  // http://support.microsoft.com/kb/105305
  int raw_out = _open_osfhandle(
      reinterpret_cast<intptr_t>(GetStdHandle(STD_OUTPUT_HANDLE)), _O_TEXT);
  *stdout = *_fdopen(raw_out, "w");
  setvbuf(stdout, NULL, _IONBF, 0);

  int raw_err = _open_osfhandle(
      reinterpret_cast<intptr_t>(GetStdHandle(STD_ERROR_HANDLE)), _O_TEXT);
  *stderr = *_fdopen(raw_err, "w");
  setvbuf(stderr, NULL, _IONBF, 0);

  int raw_in = _open_osfhandle(
      reinterpret_cast<intptr_t>(GetStdHandle(STD_INPUT_HANDLE)), _O_TEXT);
  *stdin = *_fdopen(raw_in, "r");
  setvbuf(stdin, NULL, _IONBF, 0);
  // Fix all cout, wcout, cin, wcin, cerr, wcerr, clog and wclog.
  std::ios::sync_with_stdio();
}

void OnNoMemory() {
  // Kill the process. This is important for security, since WebKit doesn't
  // NULL-check many memory allocations. If a malloc fails, returns NULL, and
  // the buffer is then used, it provides a handy mapping of memory starting at
  // address 0 for an attacker to utilize.
  __debugbreak();
  _exit(1);
}

class TimerExpiredTask : public win::ObjectWatcher::Delegate {
 public:
  explicit TimerExpiredTask(ProcessHandle process);
  ~TimerExpiredTask();

  void TimedOut();

  // MessageLoop::Watcher -----------------------------------------------------
  virtual void OnObjectSignaled(HANDLE object);

 private:
  void KillProcess();

  // The process that we are watching.
  ProcessHandle process_;

  win::ObjectWatcher watcher_;

  DISALLOW_COPY_AND_ASSIGN(TimerExpiredTask);
};

TimerExpiredTask::TimerExpiredTask(ProcessHandle process) : process_(process) {
  watcher_.StartWatching(process_, this);
}

TimerExpiredTask::~TimerExpiredTask() {
  TimedOut();
  DCHECK(!process_) << "Make sure to close the handle.";
}

void TimerExpiredTask::TimedOut() {
  if (process_)
    KillProcess();
}

void TimerExpiredTask::OnObjectSignaled(HANDLE object) {
  CloseHandle(process_);
  process_ = NULL;
}

void TimerExpiredTask::KillProcess() {
  // Stop watching the process handle since we're killing it.
  watcher_.StopWatching();

  // OK, time to get frisky.  We don't actually care when the process
  // terminates.  We just care that it eventually terminates, and that's what
  // TerminateProcess should do for us. Don't check for the result code since
  // it fails quite often. This should be investigated eventually.
  base::KillProcess(process_, kProcessKilledExitCode, false);

  // Now, just cleanup as if the process exited normally.
  OnObjectSignaled(process_);
}

}  // namespace

ProcessId GetCurrentProcId() {
  return ::GetCurrentProcessId();
}

ProcessHandle GetCurrentProcessHandle() {
  return ::GetCurrentProcess();
}

bool OpenProcessHandle(ProcessId pid, ProcessHandle* handle) {
  // We try to limit privileges granted to the handle. If you need this
  // for test code, consider using OpenPrivilegedProcessHandle instead of
  // adding more privileges here.
  ProcessHandle result = OpenProcess(PROCESS_DUP_HANDLE | PROCESS_TERMINATE,
                                     FALSE, pid);

  if (result == INVALID_HANDLE_VALUE)
    return false;

  *handle = result;
  return true;
}

bool OpenPrivilegedProcessHandle(ProcessId pid, ProcessHandle* handle) {
  ProcessHandle result = OpenProcess(PROCESS_DUP_HANDLE |
                                     PROCESS_TERMINATE |
                                     PROCESS_QUERY_INFORMATION |
                                     PROCESS_VM_READ |
                                     SYNCHRONIZE,
                                     FALSE, pid);

  if (result == INVALID_HANDLE_VALUE)
    return false;

  *handle = result;
  return true;
}

bool OpenProcessHandleWithAccess(ProcessId pid,
                                 uint32 access_flags,
                                 ProcessHandle* handle) {
  ProcessHandle result = OpenProcess(access_flags, FALSE, pid);

  if (result == INVALID_HANDLE_VALUE)
    return false;

  *handle = result;
  return true;
}

void CloseProcessHandle(ProcessHandle process) {
  CloseHandle(process);
}

ProcessId GetProcId(ProcessHandle process) {
  // Get a handle to |process| that has PROCESS_QUERY_INFORMATION rights.
  HANDLE current_process = GetCurrentProcess();
  HANDLE process_with_query_rights;
  if (DuplicateHandle(current_process, process, current_process,
                      &process_with_query_rights, PROCESS_QUERY_INFORMATION,
                      false, 0)) {
    DWORD id = GetProcessId(process_with_query_rights);
    CloseHandle(process_with_query_rights);
    return id;
  }

  // We're screwed.
  NOTREACHED();
  return 0;
}

bool GetProcessIntegrityLevel(ProcessHandle process, IntegrityLevel *level) {
  if (!level)
    return false;

  if (win::GetVersion() < base::win::VERSION_VISTA)
    return false;

  HANDLE process_token;
  if (!OpenProcessToken(process, TOKEN_QUERY | TOKEN_QUERY_SOURCE,
      &process_token))
    return false;

  win::ScopedHandle scoped_process_token(process_token);

  DWORD token_info_length = 0;
  if (GetTokenInformation(process_token, TokenIntegrityLevel, NULL, 0,
                          &token_info_length) ||
      GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    return false;

  scoped_array<char> token_label_bytes(new char[token_info_length]);
  if (!token_label_bytes.get())
    return false;

  TOKEN_MANDATORY_LABEL* token_label =
      reinterpret_cast<TOKEN_MANDATORY_LABEL*>(token_label_bytes.get());
  if (!token_label)
    return false;

  if (!GetTokenInformation(process_token, TokenIntegrityLevel, token_label,
                           token_info_length, &token_info_length))
    return false;

  DWORD integrity_level = *GetSidSubAuthority(token_label->Label.Sid,
      (DWORD)(UCHAR)(*GetSidSubAuthorityCount(token_label->Label.Sid)-1));

  if (integrity_level < SECURITY_MANDATORY_MEDIUM_RID) {
    *level = LOW_INTEGRITY;
  } else if (integrity_level >= SECURITY_MANDATORY_MEDIUM_RID &&
      integrity_level < SECURITY_MANDATORY_HIGH_RID) {
    *level = MEDIUM_INTEGRITY;
  } else if (integrity_level >= SECURITY_MANDATORY_HIGH_RID) {
    *level = HIGH_INTEGRITY;
  } else {
    NOTREACHED();
    return false;
  }

  return true;
}

bool SetJobObjectAsKillOnJobClose(HANDLE job_object) {
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit_info = {0};
  limit_info.BasicLimitInformation.LimitFlags =
      JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  return 0 != SetInformationJobObject(
      job_object,
      JobObjectExtendedLimitInformation,
      &limit_info,
      sizeof(limit_info));
}

// Attempts to kill the process identified by the given process
// entry structure, giving it the specified exit code.
// Returns true if this is successful, false otherwise.
bool KillProcessById(ProcessId process_id, int exit_code, bool wait) {
  HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE,
                               FALSE,  // Don't inherit handle
                               process_id);
  if (!process) {
    DLOG(ERROR) << "Unable to open process " << process_id << " : "
                << GetLastError();
    return false;
  }
  bool ret = KillProcess(process, exit_code, wait);
  CloseHandle(process);
  return ret;
}

bool KillProcess(ProcessHandle process, int exit_code, bool wait) {
  bool result = (TerminateProcess(process, exit_code) != FALSE);
  if (result && wait) {
    // The process may not end immediately due to pending I/O
    if (WAIT_OBJECT_0 != WaitForSingleObject(process, 60 * 1000))
      DLOG(ERROR) << "Error waiting for process exit: " << GetLastError();
  } else if (!result) {
    DLOG(ERROR) << "Unable to terminate process: " << GetLastError();
  }
  return result;
}

TerminationStatus GetTerminationStatus(ProcessHandle handle, int* exit_code) {
  DWORD tmp_exit_code = 0;

  if (!::GetExitCodeProcess(handle, &tmp_exit_code)) {
    NOTREACHED();
    if (exit_code) {
      // This really is a random number.  We haven't received any
      // information about the exit code, presumably because this
      // process doesn't have permission to get the exit code, or
      // because of some other cause for GetExitCodeProcess to fail
      // (MSDN docs don't give the possible failure error codes for
      // this function, so it could be anything).  But we don't want
      // to leave exit_code uninitialized, since that could cause
      // random interpretations of the exit code.  So we assume it
      // terminated "normally" in this case.
      *exit_code = kNormalTerminationExitCode;
    }
    // Assume the child has exited normally if we can't get the exit
    // code.
    return TERMINATION_STATUS_NORMAL_TERMINATION;
  }
  if (tmp_exit_code == STILL_ACTIVE) {
    DWORD wait_result = WaitForSingleObject(handle, 0);
    if (wait_result == WAIT_TIMEOUT) {
      if (exit_code)
        *exit_code = wait_result;
      return TERMINATION_STATUS_STILL_RUNNING;
    }

    DCHECK_EQ(WAIT_OBJECT_0, wait_result);

    // Strange, the process used 0x103 (STILL_ACTIVE) as exit code.
    NOTREACHED();

    return TERMINATION_STATUS_ABNORMAL_TERMINATION;
  }

  if (exit_code)
    *exit_code = tmp_exit_code;

  switch (tmp_exit_code) {
    case kNormalTerminationExitCode:
      return TERMINATION_STATUS_NORMAL_TERMINATION;
    case kDebuggerInactiveExitCode:  // STATUS_DEBUGGER_INACTIVE.
    case kKeyboardInterruptExitCode:  // Control-C/end session.
    case kDebuggerTerminatedExitCode:  // Debugger terminated process.
    case kProcessKilledExitCode:  // Task manager kill.
      return TERMINATION_STATUS_PROCESS_WAS_KILLED;
    default:
      // All other exit codes indicate crashes.
      return TERMINATION_STATUS_PROCESS_CRASHED;
  }
}

bool WaitForExitCode(ProcessHandle handle, int* exit_code) {
  bool success = WaitForExitCodeWithTimeout(handle, exit_code, INFINITE);
  CloseProcessHandle(handle);
  return success;
}

bool WaitForExitCodeWithTimeout(ProcessHandle handle, int* exit_code,
                                int64 timeout_milliseconds) {
  if (::WaitForSingleObject(handle, timeout_milliseconds) != WAIT_OBJECT_0)
    return false;
  DWORD temp_code;  // Don't clobber out-parameters in case of failure.
  if (!::GetExitCodeProcess(handle, &temp_code))
    return false;

  *exit_code = temp_code;
  return true;
}

ProcessIterator::ProcessIterator(const ProcessFilter* filter)
    : started_iteration_(false),
      filter_(filter) {
  snapshot_ = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
}

ProcessIterator::~ProcessIterator() {
  CloseHandle(snapshot_);
}

bool ProcessIterator::CheckForNextProcess() {
  InitProcessEntry(&entry_);

  if (!started_iteration_) {
    started_iteration_ = true;
    return !!Process32First(snapshot_, &entry_);
  }

  return !!Process32Next(snapshot_, &entry_);
}

void ProcessIterator::InitProcessEntry(ProcessEntry* entry) {
  memset(entry, 0, sizeof(*entry));
  entry->dwSize = sizeof(*entry);
}

bool NamedProcessIterator::IncludeEntry() {
  // Case insensitive.
  return _wcsicmp(executable_name_.c_str(), entry().exe_file()) == 0 &&
         ProcessIterator::IncludeEntry();
}

bool WaitForProcessesToExit(const std::wstring& executable_name,
                            int64 wait_milliseconds,
                            const ProcessFilter* filter) {
  const ProcessEntry* entry;
  bool result = true;
  DWORD start_time = GetTickCount();

  NamedProcessIterator iter(executable_name, filter);
  while ((entry = iter.NextProcessEntry())) {
    DWORD remaining_wait =
        std::max<int64>(0, wait_milliseconds - (GetTickCount() - start_time));
    HANDLE process = OpenProcess(SYNCHRONIZE,
                                 FALSE,
                                 entry->th32ProcessID);
    DWORD wait_result = WaitForSingleObject(process, remaining_wait);
    CloseHandle(process);
    result = result && (wait_result == WAIT_OBJECT_0);
  }

  return result;
}

bool WaitForSingleProcess(ProcessHandle handle, int64 wait_milliseconds) {
  int exit_code;
  if (!WaitForExitCodeWithTimeout(handle, &exit_code, wait_milliseconds))
    return false;
  return exit_code == 0;
}

bool CleanupProcesses(const std::wstring& executable_name,
                      int64 wait_milliseconds,
                      int exit_code,
                      const ProcessFilter* filter) {
  bool exited_cleanly = WaitForProcessesToExit(executable_name,
                                               wait_milliseconds,
                                               filter);
  if (!exited_cleanly)
    KillProcesses(executable_name, exit_code, filter);
  return exited_cleanly;
}

void EnsureProcessTerminated(ProcessHandle process) {
  DCHECK(process != GetCurrentProcess());

  // If already signaled, then we are done!
  if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
    CloseHandle(process);
    return;
  }

  MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&TimerExpiredTask::TimedOut,
                 base::Owned(new TimerExpiredTask(process))),
      kWaitInterval);
}

bool EnableLowFragmentationHeap() {
  HMODULE kernel32 = GetModuleHandle(L"kernel32.dll");
  HeapSetFn heap_set = reinterpret_cast<HeapSetFn>(GetProcAddress(
      kernel32,
      "HeapSetInformation"));

  // On Windows 2000, the function is not exported. This is not a reason to
  // fail.
  if (!heap_set)
    return true;

  unsigned number_heaps = GetProcessHeaps(0, NULL);
  if (!number_heaps)
    return false;

  // Gives us some extra space in the array in case a thread is creating heaps
  // at the same time we're querying them.
  static const int MARGIN = 8;
  scoped_array<HANDLE> heaps(new HANDLE[number_heaps + MARGIN]);
  number_heaps = GetProcessHeaps(number_heaps + MARGIN, heaps.get());
  if (!number_heaps)
    return false;

  for (unsigned i = 0; i < number_heaps; ++i) {
    ULONG lfh_flag = 2;
    // Don't bother with the result code. It may fails on heaps that have the
    // HEAP_NO_SERIALIZE flag. This is expected and not a problem at all.
    heap_set(heaps[i],
             HeapCompatibilityInformation,
             &lfh_flag,
             sizeof(lfh_flag));
  }
  return true;
}

void EnableTerminationOnHeapCorruption() {
  // Ignore the result code. Supported on XP SP3 and Vista.
  HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);
}

void EnableTerminationOnOutOfMemory() {
  std::set_new_handler(&OnNoMemory);
}

bool EnableInProcessStackDumping() {
  // Add stack dumping support on exception on windows. Similar to OS_POSIX
  // signal() handling in process_util_posix.cc.
  g_previous_filter = SetUnhandledExceptionFilter(&StackDumpExceptionFilter);
  AttachToConsole();
  return true;
}

void RaiseProcessToHighPriority() {
  SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
}

// GetPerformanceInfo is not available on WIN2K.  So we'll
// load it on-the-fly.
const wchar_t kPsapiDllName[] = L"psapi.dll";
typedef BOOL (WINAPI *GetPerformanceInfoFunction) (
    PPERFORMANCE_INFORMATION pPerformanceInformation,
    DWORD cb);

// Beware of races if called concurrently from multiple threads.
static BOOL InternalGetPerformanceInfo(
    PPERFORMANCE_INFORMATION pPerformanceInformation, DWORD cb) {
  static GetPerformanceInfoFunction GetPerformanceInfo_func = NULL;
  if (!GetPerformanceInfo_func) {
    HMODULE psapi_dll = ::GetModuleHandle(kPsapiDllName);
    if (psapi_dll)
      GetPerformanceInfo_func = reinterpret_cast<GetPerformanceInfoFunction>(
          GetProcAddress(psapi_dll, "GetPerformanceInfo"));

    if (!GetPerformanceInfo_func) {
      // The function could be loaded!
      memset(pPerformanceInformation, 0, cb);
      return FALSE;
    }
  }
  return GetPerformanceInfo_func(pPerformanceInformation, cb);
}

size_t GetSystemCommitCharge() {
  // Get the System Page Size.
  SYSTEM_INFO system_info;
  GetSystemInfo(&system_info);

  PERFORMANCE_INFORMATION info;
  if (!InternalGetPerformanceInfo(&info, sizeof(info))) {
    DLOG(ERROR) << "Failed to fetch internal performance info.";
    return 0;
  }
  return (info.CommitTotal * system_info.dwPageSize) / 1024;
}

}  // namespace base
