// Sandstorm - Personal Cloud Sandbox
// Copyright (c) 2014 Sandstorm Development Group, Inc. and contributors
// All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SANDSTORM_UTIL_H_
#define SANDSTORM_UTIL_H_
// This file contains various utility functions used in Sandstorm.
//
// TODO(cleanup): A lot of stuff in here should move into KJ, after proper cleanup.

#include <kj/io.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <kj/debug.h>
#include <kj/vector.h>
#include <unistd.h>
#include <kj/function.h>
#include <kj/async.h>

namespace kj {
  class UnixEventPort;
}

namespace sandstorm {

#if __QTCREATOR
#define KJ_MVCAP(var) var
// QtCreator dosen't understand C++14 syntax yet.
#else
#define KJ_MVCAP(var) var = ::kj::mv(var)
// Capture the given variable by move.  Place this in a lambda capture list.  Requires C++14.
//
// TODO(cleanup):  Move to libkj.
#endif

typedef unsigned int uint;
typedef unsigned char byte;

struct Pipe {
  kj::AutoCloseFd readEnd;
  kj::AutoCloseFd writeEnd;

  static Pipe make();
  static Pipe makeAsync();
};

kj::AutoCloseFd raiiOpen(kj::StringPtr name, int flags, mode_t mode = 0666);
kj::AutoCloseFd raiiOpenAt(int dirfd, kj::StringPtr name, int flags, mode_t mode = 0666);

kj::Maybe<kj::AutoCloseFd> raiiOpenIfExists(
    kj::StringPtr name, int flags, mode_t mode = 0666);
kj::Maybe<kj::AutoCloseFd> raiiOpenAtIfExists(
    int dirfd, kj::StringPtr name, int flags, mode_t mode = 0666);

kj::Maybe<kj::String> readLine(kj::BufferedInputStream& input);

class StructyMessage {
  // Helper for constructing a message to be passed to the kernel composed of a bunch of structs
  // back-to-back.

public:
  explicit StructyMessage(uint alignment = 8): alignment(alignment) {
    memset(bytes, 0, sizeof(bytes));
  }

  template <typename T>
  T* add() {
    T* result = reinterpret_cast<T*>(pos);
    pos += (sizeof(T) + (alignment - 1)) & ~(alignment - 1);
    KJ_ASSERT(pos - bytes <= sizeof(bytes));
    return result;
  }

  void addString(const char* data) {
    addBytes(data, strlen(data));
  }
  void addBytes(const void* data, size_t size) {
    memcpy(pos, data, size);
    pos += (size + (alignment - 1)) & ~(alignment - 1);
  }

  void* begin() { return bytes; }
  void* end() { return pos; }
  size_t size() { return pos - bytes; }

private:
  char bytes[4096];
  char* pos = bytes;
  uint alignment;
};

inline size_t offsetBetween(void* start, void* end) {
  return reinterpret_cast<char*>(end) - reinterpret_cast<char*>(start);
}

constexpr const char* EXEC_END_ARGS = nullptr;

kj::String trim(kj::ArrayPtr<const char> slice);
kj::ArrayPtr<const char> trimArray(kj::ArrayPtr<const char> slice);
// Remove whitespace from both ends of the char array and return what's left as a String.

void toLower(kj::ArrayPtr<char> text);
// Force entire array of chars to lower-case.

kj::Maybe<uint> parseUInt(kj::StringPtr s, int base);
// Try to parse an integer with strtoul(), return null if parsing fails or doesn't consume all
// input.

kj::AutoCloseFd openTemporary(kj::StringPtr near);
// Creates a temporary file in the same directory as the file specified by "near", immediately
// unlinks it, and then returns the file descriptor,  which will be open for both read and write.

bool isDirectory(kj::StringPtr path);

kj::Array<kj::String> listDirectory(kj::StringPtr dirname);
// Get names of all files in the given directory except for "." and "..".

kj::Array<kj::String> listDirectoryFd(int dirfd);
// Like `listDirectory()` but operates on a file descriptor.

void recursivelyDelete(kj::StringPtr path);
// Delete the given path, recursively if it is a directory.
//
// Since this may be used in KJ_DEFER to delete temporary directories, all exceptions are
// recoverable (won't throw if already unwinding).

void recursivelyCreateParent(kj::StringPtr path);
// Create the parent directory of `path` if it doesn't exist, and the parent's parent, and so on.

kj::String readAll(int fd);
// Read entire contents of the file descirptor to a String.

kj::String readAll(kj::StringPtr name);
// Read entire contents of a named file to a String.

kj::Array<kj::String> splitLines(kj::String input);
// Split the input into lines, trimming whitespace, and ignoring blank lines or lines that start
// with #. Consumes the input string.

kj::Vector<kj::ArrayPtr<const char>> split(kj::ArrayPtr<const char> input, char delim);
// Split the char array on an arbitrary delimiter character.

kj::Maybe<kj::ArrayPtr<const char>> splitFirst(kj::ArrayPtr<const char>& input, char delim);
// Split the char array on the first instance of the delimiter. `input` is updated in-place to
// point at the remainder of the array while the prefix that was split off is returned. If the
// delimiter doesn't appear, returns null.

kj::ArrayPtr<const char> extractHostFromUrl(kj::StringPtr url);
kj::ArrayPtr<const char> extractProtocolFromUrl(kj::StringPtr url);

kj::String base64Encode(kj::ArrayPtr<const byte> input, bool breakLines);
// Encode the input as base64. If `breakLines` is true, insert line breaks every 72 characters and
// at the end of the output. (Otherwise, return one long line.)

kj::Array<byte> base64Decode(kj::StringPtr input);
// Decode base64 input to bytes. Non-base64 characters in the input will be ignored.

class Subprocess {
public:
  struct Options {
    kj::StringPtr executable;
    // Executable file name.

    bool searchPath = true;
    // Whether to search for `executable` in the `PATH` (e.g. use `execvp()` rather than
    // `execv()`). If `executable` contains a '/' character, this has no effect (`PATH` is never
    // searched).

    kj::ArrayPtr<const kj::StringPtr> argv;
    // Arguments to the program. By convention, the first argument should be the same as
    // `executable`.

    int stdin = STDIN_FILENO;
    int stdout = STDOUT_FILENO;
    int stderr = STDERR_FILENO;
    // What file descriptors to substitute for standard I/O.
    //
    // Note that if you override these, then the overridden FD is expected to be close-on-exec.
    // `Subprocess` does NOT close the old FD after dup2()ing it over the standard I/O FD.

    kj::ArrayPtr<int> moreFds;
    // You may pass additional FDs (3, 4, 5, 6, ...) using this array. `Subprocess` will
    // automatically deal with re-arranging file descriptors as needed, so if e.g. you pass in
    // the array {4, 3}, it will correctly swap the two descriptors. Again, it is expected that
    // all the file descriptors in this array (and indeed all file descriptors process-wide except
    // for 0, 1, and 2) are marked close-on-exec.

    kj::Maybe<kj::ArrayPtr<const kj::StringPtr>> environment;
    // An array of 'NAME=VALUE' pairs specifying the child's environment. If null, inherits the
    // parent's environment.

    Options(kj::StringPtr executable): executable(executable), argv(&this->executable, 1) {}
    Options(kj::ArrayPtr<const kj::StringPtr> argv): executable(argv[0]), argv(argv) {}
    Options(kj::Array<const kj::StringPtr>&& argv)
        : executable(argv[0]), argv(argv), ownArgv(kj::mv(argv)) {}
    Options(std::initializer_list<const kj::StringPtr> argv)
        : Options(kj::heapArray(argv)) {}

  private:
    kj::Array<const kj::StringPtr> ownArgv;
  };

  Subprocess(Options&& options);
  // Start a subprocess based on the given options.

  Subprocess(std::initializer_list<const kj::StringPtr> argv)
      : Subprocess(Options(kj::mv(argv))) {}
  // Start a subprocess given a simple command argument array. The first argument is the executable
  // name.

  Subprocess(kj::Function<int()> func);
  // Start a fork()ed subprocess that runs the given function then exits. Unlike the other
  // constructors, this constructor does not call exec()! Note that `func` is destroyed in the
  // parent process before this returns, since it is only needed in the child process. Note also
  // that under no circumstances will destructors of stack or global objects present before the
  // fork be executed inside the child process -- the child cannot unwind the stack with an
  // exception, and exits using _exit() to avoid global destructors.

  explicit Subprocess(pid_t pid): pid(pid) {}
  // Adopt a child process created by some other means.
  //
  // Be careful not to adopt a pid that has potentially already died and been reaped. Keep in mind
  // that if a SubprocessSet exists in the parent process then it is actively reaping childern at
  // all times. Remember than you can do kill(pid, 0) to check if the pid exists (although this is
  // problematic if it's been long enough that the kernel pid counter may have looped).

  KJ_DISALLOW_COPY(Subprocess);

  inline Subprocess(Subprocess&& other)
      : name(kj::mv(other.name)), pid(other.pid) { other.pid = 0; }

  ~Subprocess() noexcept(false);
  // Kills the subprocess (with SIGKILL) and waitpid()s it if it hasn't already finished.

  void signal(int signo);
  // Sends the given signal to the child process.

  void waitForSuccess();
  // Wait for the child to exit. Throws an exception if it returns a non-zero exit status or is
  // killed by a signal.

  int waitForExit() KJ_WARN_UNUSED_RESULT;
  // Waits for the child to exit and returns the exit status. Throws an exception if it is killed
  // by a signal.

  int waitForExitOrSignal() KJ_WARN_UNUSED_RESULT;
  // Waits for the child to exit or be killed by a signal. Returns an exit status that can be
  // interpreted by WIFEXITED(), WEXITSTATUS(), etc. as described in the wait(2) man page.

  pid_t getPid() {
    KJ_IREQUIRE(pid != 0, "already exited");
    return pid;
  }

  bool isRunning() {
    return pid != 0;
  }

  void notifyExited(int status) {
    // Call if you receive exit notification from elsewhere, e.g. calling wait() yourself. It is
    // NECESSARY to call this immediately upon receiving an exit notification, otherwise the
    // destructor will try to SIGKILL the pid which might have been re-assigned by then.
    //
    // TODO(cleanup): Build a safer API to allow waiting on a group of subprocesses, or using
    //   async I/O.

    pid = 0;
  }

  void detach() {
    // Indicates that you don't intend to wait for this process to complete and do not want it to
    // be killed when the Subprocess object is destroyed. The parent process needs a wait loop
    // somewhere to clean up zombies.

    pid = 0;
  }

private:
  kj::String name;
  kj::UnwindDetector unwindDetector;
  pid_t pid = 0;  // 0 = not running

  static void forceFdAbove(int& fd, int minValue);

  friend class SubprocessSet;
};

class SubprocessSet {
  // Represents a set of subprocesses and allows you to asynchronously wait for them to complete.
  // In order to use SubprocessSet, it is necessary that *all* subprocesses of this process are
  // managed through it, and wait() is always called immediately on creation of a new subprocess.
  //
  // TODO(cleanup): This functionality should be merged into KJ's async I/O library.

public:
  explicit SubprocessSet(kj::UnixEventPort& eventPort);
  ~SubprocessSet() noexcept(false);
  KJ_DISALLOW_COPY(SubprocessSet);

  kj::Promise<void> waitForSuccess(Subprocess& subprocess);
  kj::Promise<int> waitForExit(Subprocess& subprocess);
  kj::Promise<int> waitForExitOrSignal(Subprocess& subprocess);

  kj::Promise<void> waitForSuccess(Subprocess&& subprocess);
  kj::Promise<int> waitForExit(Subprocess&& subprocess);
  kj::Promise<int> waitForExitOrSignal(Subprocess&& subprocess);

private:
  struct WaitMap;
  kj::UnixEventPort& eventPort;
  kj::Own<WaitMap> waitMap;
  kj::Promise<void> waitTask;

  kj::Promise<void> waitLoop();
};

}  // namespace sandstorm

#endif // SANDSTORM_UTIL_H_
