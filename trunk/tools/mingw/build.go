package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

const (
	OUT_DIR_VAR = "OUT_DIR"
)

var (
	MINGW32_CC  = "i686-w64-mingw32-gcc"
	MINGW32_CPP = "i686-w64-mingw32-g++"
)

// Context is used to pass variables to tasks. Variable is identified by string
// and its value can be of any type. Context values are hierarchial i.e. they
// might start with a default, globally visible values and then some variables
// can be modified for a subtasks.
// Imagine a c compilation task. We start with a standard compiler flags for
// all files but we need to be able to over-ride them for all files in a given
// directory or even for a specific file
type Context map[string]interface{}

func NewContext() Context {
	return make(map[string]interface{})
}

// Dup creates a copy
func (c Context) Dup() Context {
	res := make(map[string]interface{}, len(c))
	for k, v := range c {
		res[k] = v
	}
	return res
}

// GetStrVal() returns a given context variable and casts it to string
// Returns "" if variable doesn't exist or isn't a string
func (c Context) GetStrVal(k string) string {
	if v, exists := c[k]; exists {
		if s, ok := v.(string); ok {
			return s
		}
	}
	return ""
}

func (c Context) GetStrValMust(k string) string {
	if v, exists := c[k]; exists {
		if s, ok := v.(string); ok {
			return s
		}
	}
	panicif(true, fmt.Sprintf("value for %q doesn't exist or is not a string", k))
	return ""
}

// Task is a single build operation e.g. running compiler for a given .c file
type Task interface {
	// Run executes the task. Most tasks involve executing other programs (like
	// a C compiler) so we also return stdout and stderr for diagnostic
	Run() (error, []byte, []byte)
	SetContext(ctx Context)
}

// default implementation of context that can be embedded in all implementation of Task
type TaskContext struct {
	Ctx Context
}

func (tc *TaskContext) SetContext(ctx Context) {
	tc.Ctx = ctx
}

// MkdirTask creates a directory
type MkdirTask struct {
	TaskContext
	Dir string
}

func (t *MkdirTask) Run() (error, []byte, []byte) {
	dir := t.Dir
	panicif(dir == "")
	fmt.Printf("mkdir %s\n", dir)
	err := os.MkdirAll(dir, 0755)
	if os.IsExist(err) {
		err = nil
	}
	return err, nil, nil
}

type MkdirOutTask struct {
	TaskContext
}

func (t *MkdirOutTask) Run() (error, []byte, []byte) {
	dir := t.Ctx.GetStrValMust(OUT_DIR_VAR)
	fmt.Printf("mkdir %s\n", dir)
	err := os.MkdirAll(dir, 0755)
	if os.IsExist(err) {
		err = nil
	}
	return err, nil, nil
}

// Tasks is a sequence of tasks. They all share a common context
// TODO: add notion of serial and parallel tasks; execute parallel tasks in parallel
type Tasks struct {
	TaskContext
	ToRun []Task
}

func combineOut(curr, additional []byte) []byte {
	if len(curr) == 0 {
		return additional
	}
	if len(additional) == 0 {
		return additional
	}
	curr = append(curr, '\n')
	return append(curr, additional...)
}

func (t *Tasks) Run() (error, []byte, []byte) {
	var stdoutCombined []byte
	var stderrCombined []byte
	for _, task := range t.ToRun {
		// propagate the context to all children
		task.SetContext(t.Ctx)
		err, stdout, stderr := task.Run()
		if err != nil {
			return err, stdout, stderr
		}
		stdoutCombined = combineOut(stdoutCombined, stdout)
		stderrCombined = combineOut(stderrCombined, stderr)
	}
	return nil, stdoutCombined, stderrCombined
}

func cmdString(cmd *exec.Cmd) string {
	arr := []string{cmd.Path}
	args := cmd.Args[1:]
	arr = append(arr, args...)
	return strings.Join(arr, " ")
}

func panicif(cond bool, msgs ...string) {
	if cond {
		var msg string
		if len(msgs) > 0 {
			msg = msgs[0]
		} else {
			msg = "error"
		}
		panic(msg)
	}
}

func genOut(dir, in, ext string) string {
	panicif(ext[0] != '.')
	in = filepath.Base(in)
	e := filepath.Ext(in)
	in = in[:len(in)-len(e)]
	return filepath.Join(dir, in+ext)
}

// MingwCcTask compiles a c/c++ file with mingw
type MingwCcTask struct {
	TaskContext
	In  string
	Out string
	// TODO: move IncDirs to Context
	IncDirs string
}

func (t *MingwCcTask) Run() (error, []byte, []byte) {
	outDir := t.Ctx.GetStrValMust(OUT_DIR_VAR)
	if t.Out == "" {
		t.Out = genOut(outDir, t.In, ".o")
		// TODO: ensure t.Out hasn't be generated in other tasks
	}
	// use gcc for *.c files, g++ for everything else
	cc := MINGW32_CPP
	// TODO: use case-insensitive check
	if strings.HasSuffix(t.In, ".c") {
		cc = MINGW32_CC
	}
	args := []string{}
	if t.IncDirs != "" {
		dirs := strings.Split(t.IncDirs, ";")
		for _, dir := range dirs {
			args = append(args, "-I", dir)
		}
	}
	args = append(args, "-c", t.In, "-o", t.Out)
	cmd := exec.Command(cc, args...)
	fmt.Println(cmdString(cmd))
	// TODO: get stdout, stderr separately
	stdout, err := cmd.CombinedOutput()
	return err, stdout, nil
}

func main() {
	fmt.Printf("Hello to a build system\n")
	ctx := NewContext()
	ctx[OUT_DIR_VAR] = "rel"
	rootTask := Tasks{
		TaskContext: TaskContext{ctx},
		ToRun: []Task{
			&MkdirOutTask{},
			&MingwCcTask{In: "src/ChmDoc.cpp", IncDirs: "src/utils;ext/CHMLib/src"},
			&MingwCcTask{In: "ext/zlib/adler32.c"},
			&MingwCcTask{In: "src/AppPrefs.cpp", IncDirs: "src/utils"},
			&MingwCcTask{In: "src/AppTools.cpp", IncDirs: "src/utils"},
			&MingwCcTask{In: "src/AppUtil.cpp", IncDirs: "src/utils"},
			&MingwCcTask{In: "src/Caption.cpp", IncDirs: "src/utils"},
		},
	}
	err, stdout, stderr := rootTask.Run()
	if err != nil {
		fmt.Printf("Failed with %q, stdout:\n%s\nstderr:\n%s\n", err, string(stdout), string(stderr))
	}
}
