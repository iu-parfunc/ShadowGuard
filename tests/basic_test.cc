
// Test Name : basic_test
//
// Description :
//
// Tests hardening an application with simple function call chain

int foo(int param) { return param; }

int bar(int param) { return foo(param) }

int baz(int param) { return baz(param); }

int main() { baz(42); }
