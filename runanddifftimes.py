# Writen by Krzysztof Kowalczyk (http://blog.kowalczyk.info)

# Usage:
# runanddifftimes.py <exe-one> <exe-two> <pdf-file> [<count>] -loadonly
# <exe-one> and <exe-two> are paths to pdftest.exe
# <pdf-file> is path to PDF file to test
# optional <count> is a number of times to run the tests
# optional [-loadonly] makes us pass -loadonly flag to <exe-one> and <exe-two>

# We run <exe-one> and <exe-two> on <pdf-file> <count> times.
# Parameters to <exe-one> and <exe-two> are "-timings -preview"
# plus "-loadonly" if -loadonly flag was provided
# We save the results to files <NN>-time-0.txt and <NN>-time-1.txt
# and run difftimes.py parse_stats_from_file() on those two files
# to show time differences.
# <NN> is chosen so that the file names are unique (to avoid overwriting)

import sys, os, os.path, string, difftimes
from subprocess import *

DEFAULT_COUNT = 6
MAX_COUNT = 51

def usage_and_exit():
  print "Usage: runanddifftimes.py <exe-one> <exe-two> <pdf-file> [<count>] [-loadonly]"
  sys.exit(1)

def ensure_valid_count(count):
  if count < 1 or count >= MAX_COUNT:
    print "Error: count value of %d is rather silly. It should be > 0 and < %d" % (count, MAX_COUNT)
    sys.exit(1)
    
def ensure_file_exists(file_name):
  if not os.path.isfile(file_name):
    print "file '%s' doesn't exist" % file_name
    usage_and_exit()

def run(exe, pdf, loadOnly):
  args = [exe, "-timings"]
  if loadOnly: 
    args.append("-loadonly")
  else:
    args.append("-preview")
  args.append(pdf)
  print "executing '%s'" % string.join(args, " ")
  return Popen(args, stdout=PIPE).communicate()[0]

def find_unique_names():
  # TODO: find non-clashing file names
  file_one = "00-timings-0.txt"
  file_two = "00-timings-1.txt"
  return (file_one, file_two)

def save_to_file(file_name, txt):
  fo = open(file_name, "wb")
  fo.write(txt)
  fo.close()

def save_results_to_file(one_txt, two_txt):
  (file_one, file_two) = find_unique_names()
  save_to_file(file_one, one_txt)
  save_to_file(file_two, two_txt)
  return (file_one, file_two)

def detect_remove_cmd_flag(flag):
    flag_present = False
    try:
        pos = sys.argv.index(flag)
        flag_present = True
        sys.argv[pos:pos+1] = []
    except:
        pass
    return flag_present

def main():
  load_only = detect_remove_cmd_flag("-loadonly")
  if len(sys.argv) < 4 or len(sys.argv) > 6:
    usage_and_exit()
  exe_one = sys.argv[1]
  exe_two = sys.argv[2]
  pdf_file = sys.argv[3]
  if 5 == len(sys.argv):
    count = int(sys.argv[4])
  else:
    count = DEFAULT_COUNT

  ensure_valid_count(count)
  ensure_file_exists(exe_one)
  ensure_file_exists(exe_two)
  ensure_file_exists(pdf_file)

  one_out = []
  two_out = []
  for c in range(count):
    out = run(exe_one, pdf_file, load_only)
    if None == out:
      print "run(%s, %s) failed" % (exe_one, pdf_file)
      sys.exit(1)
    one_out.append(out)
    out = run(exe_two, pdf_file, load_only)
    if None == out:
      print "run(%s, %s) failed" % (exe_two, pdf_file)
      sys.exit(1)
    two_out.append(out)
  one_out_txt = string.join(one_out, "\n")
  two_out_txt = string.join(two_out, "\n")
  print one_out_txt
  print two_out_txt
  (file_one, file_two) = save_results_to_file(one_out_txt, two_out_txt)
  difftimes.compare_stats(file_one, file_two)

if __name__ == "__main__":
  main()
