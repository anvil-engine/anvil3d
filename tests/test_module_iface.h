#pragma once

// Shared between test_module_lib and test_interfaces: a vtable crossing the module boundary.
struct ITestInterface {
  virtual int answer() = 0;

protected:
  ~ITestInterface() = default;
};
