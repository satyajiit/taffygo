// A sandboxed worker's interpreter: no path, no site, and a library that
// arrives as a descriptor.
//
// This is the shape decision 0046 describes, reduced to the smallest program
// that can be run and measured. The startup set is frozen into the binary, so
// nothing is read from a filesystem to reach the point where the bootstrap can
// take over; the standard library is mapped from a descriptor and made
// importable by that bootstrap; and `sys.path` is empty from the first import
// to the last.

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "taffy_frozen.h"

// CPython's own frozen table carries importlib and the startup modules the
// interpreter cannot boot without. Replacing the pointer would lose them, so
// the two tables are joined and the interpreter is handed the result.
static const struct _frozen *taffy_join_frozen_tables(void) {
  size_t base = 0, extra = 0;
  while (PyImport_FrozenModules && PyImport_FrozenModules[base].name) base++;
  while (taffy_frozen_extra[extra].name) extra++;
  struct _frozen *joined = calloc(base + extra + 1, sizeof(struct _frozen));
  if (joined == NULL) return PyImport_FrozenModules;
  memcpy(joined, PyImport_FrozenModules, base * sizeof(struct _frozen));
  memcpy(joined + base, taffy_frozen_extra, extra * sizeof(struct _frozen));
  return joined;
}

// Stands in for the descriptor the browser opens and sends. The worker never
// receives the path; this program takes one only because it has no browser to
// be sent a descriptor by, and it closes it as soon as the mapping exists so
// that nothing after this point can name the file.
static int taffy_map_archive(const char *path, void **address, size_t *size) {
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return -1;
  struct stat info;
  if (fstat(fd, &info) != 0) { close(fd); return -1; }
  void *mapped = mmap(NULL, (size_t)info.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (mapped == MAP_FAILED) return -1;
  *address = mapped;
  *size = (size_t)info.st_size;
  return 0;
}

static const char *TAFFY_REPORT =
    "import json, sys, time\n"
    "wanted = ['json','re','textwrap','dataclasses','decimal','fractions',\n"
    "          'statistics','datetime','base64','csv','struct','typing',\n"
    "          'logging','email.parser','xml.etree.ElementTree','hashlib',\n"
    "          'unicodedata','difflib','argparse','gzip','zipfile']\n"
    "served, builtin, failed = 0, 0, []\n"
    "started = time.monotonic()\n"
    "for name in wanted:\n"
    "    try:\n"
    "        __import__(name)\n"
    "    except Exception as error:\n"
    "        failed.append(f'{name}: {type(error).__name__}: {error}')\n"
    "        continue\n"
    "    origin = getattr(sys.modules[name].__spec__, 'origin', '') or ''\n"
    "    if origin.startswith('taffy-archive:'):\n"
    "        served += 1\n"
    "    elif origin in ('frozen', 'built-in'):\n"
    "        builtin += 1\n"
    "    else:\n"
    "        failed.append(f'{name}: served from {origin!r}')\n"
    "elapsed = time.monotonic() - started\n"
    // Written to the descriptor rather than printed: on Android CPython
    // redirects sys.stdout into the system log, and a report that went there
    // would be a report nobody running this reads.
    "import os\n"
    "os.write(1, json.dumps({'served': served, 'in_binary': builtin,\n"
    "                        'wanted': len(wanted), 'failed': failed,\n"
    "                        'path': sys.path,\n"
    "                        'seconds': round(elapsed, 3)}).encode() + b'\\n')\n";

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <standard-library-archive>\n", argv[0]);
    return 2;
  }

  void *address = NULL;
  size_t size = 0;
  if (taffy_map_archive(argv[1], &address, &size) != 0) {
    fprintf(stderr, "taffy: the archive could not be mapped\n");
    return 2;
  }

  PyImport_FrozenModules = taffy_join_frozen_tables();

  PyConfig config;
  PyConfig_InitIsolatedConfig(&config);
  // The whole point: the interpreter is told it has a search path, and that
  // the path is empty. Without this it computes one from the executable's
  // location and would find whatever happened to be beside it.
  config.module_search_paths_set = 1;
  config.site_import = 0;
  config.user_site_directory = 0;
  config.write_bytecode = 0;
  config.install_signal_handlers = 0;
  config.pathconfig_warnings = 0;

  PyStatus status = Py_InitializeFromConfig(&config);
  PyConfig_Clear(&config);
  if (PyStatus_Exception(status)) {
    fprintf(stderr, "taffy: the interpreter did not start\n");
    Py_ExitStatusException(status);
  }

  PyObject *boot = PyImport_ImportModule("taffy_stdlib_boot");
  if (boot == NULL) { PyErr_Print(); return 1; }
  PyObject *view = PyMemoryView_FromMemory((char *)address, (Py_ssize_t)size, PyBUF_READ);
  if (view == NULL) { PyErr_Print(); return 1; }
  PyObject *count = PyObject_CallMethod(boot, "install", "O", view);
  if (count == NULL) { PyErr_Print(); return 1; }
  printf("members            %ld\n", PyLong_AsLong(count));
  Py_DECREF(count);
  Py_DECREF(view);
  Py_DECREF(boot);

  int result = PyRun_SimpleString(TAFFY_REPORT);
  if (Py_FinalizeEx() < 0) result = 1;
  return result == 0 ? 0 : 1;
}
