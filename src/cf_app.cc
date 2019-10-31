
void bar(int* x) { *x = 42; }

int foo(void (*fp)(int*)) {
  int x = 23;
  if (x > 42) {
    // bar(&x);
    fp(&x);
    x++;
  }
  return x;
}

int main() {
  int x = foo(&bar);
  return 0;
}
