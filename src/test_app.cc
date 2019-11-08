
void bar(int* x) { *x = 42; }

/*

Original nodes: 5
Lowered nodes : 7

Safe paths : 0
Unsafe paths : 1

*/
int foo() {
  int x = 23;
  if (x < 23) {
    bar(&x);
  } else {
    x++;
    bar(&x);
  }

  return x;
}

/*

Original nodes: 13
Lowered nodes : 17

Safe paths : 1
Unsafe paths : 1

int foo() {
  int x = 23;
  while (x > 0) {
    // bar(&x);
    x--;
    for (int i = 0; i < x; i++) {
      x--;
      // bar(&i);
    }
  }

  if (x < 0) {
    for (int j = 0; j < 3; j++) {
      bar(&j);
    }
  }

  if (x < 0) {
    x--;
  }

  return x;
}
*/

/*

Original nodes: 4
Lowered nodes : 6

Safe paths : 1
Unsafe paths : 1

int foo() {
  int x = 23;
  if (x > 42) {
    bar(&x);
    // fp(&x);
    x++;
  }
  return x;
}
*/

int main() {
  int x = foo();
  return 0;
}
