from c import printf

trait SomeTrait() {
  static void someStaticMethod() {
    print("Inside someStaticMethod")
  }

  void someInstanceMethod() {
    print("Inside someInstanceMethod")
  }
}

trait SomeOtherTrait {
  void someOtherInstanceMethod() {
    print("Inside someOtherInstanceMethod")
  }
}

class SomeClass(SomeTrait, SomeOtherTrait) {
  static SomeClass new() = $(SomeClass)
}

String message = 'hello world'
List items = []

void main() {

  assert(true)
  assert(!false)
  assert(!!true)
  assert.equal(1, 1)
  assert.equal(1, 1.0)
  assert(1 is not 1.0)
  assert.equal(1.0, 1.0)
  assert(1.1 != 1.0)
  assert('abc' == 'abc')
  assert('abcd' != 'abce')
  assert('abc' != 'abcd')
  assert('abc' != 3)

  ${
    float f = if (1) {
      2
    } else {
      1.5
    }
    assert(f is 2.0)
    assert(f is not 2)
  }

  assert.equal(str(true), 'true')
  assert.equal(str(false), 'false')

  List list = List()
  c_long size = list.size
  assert.equal(list.size, 0)
  print('list.size = ' + str(size))
  list.push(15)
  size = list.size
  print('list.size = ' + str(size))

  SomeTrait.someStaticMethod()

  SomeClass someClass = SomeClass()
  someClass.someInstanceMethod()
  someClass.someOtherInstanceMethod()
  someClass.someInstanceMethodOnObject()

  print(someClass)

  print([1, 2, 3])
  print('[1, 2, 3].size = ' + str([1, 2, 3].size))

  print('(global) message = ' + message)
  print('(global) items = ' + str(items))
  items.push(1)
  items.push(2)
  items.push(3)
  print('(global) items = ' + str(items))

  // ${
  //   int i = 0
  //   int total = 0
  //   while (i < 1000000000) {
  //     total = total + i
  //     i = i + 1
  //   }
  //   print('total = ' + str(total))
  // }

  ${
    var a = 10
    var b = 5
    int ia = a

    print(def(int c) {
      a
    })
    print(def int(int c) {
      print('inside lambda function')
      ia + c
    }(196))

    var f = def int(int a, int b) = a + b
    print('f = ' + str(f))
    print(f(494, 489))
    printf(c'a is b = %d\n', dynamic_cast(c_int, a is b))
    printf(c'a is not b = %d\n', dynamic_cast(c_int, a is not b))
    printf(c'a is b = %d\n', dynamic_cast(c_int, a is a))
  }

  ${
    var g = (a, b) => a + b
    print('g = ' + str(g))
    print('g(8781, 498) = ' + str(g(8781, 498)))
    print(
      ((a, b) => a * b)(5.5, 7.254)
    )
  }

  ${
    print('a')
    int i = 0
    while (i < 10) {
      print("i = " + str(i))
      i = i + 1
    }
  }

  ${
    type t = SomeOtherTrait
    var tv = t
    t = tv
    print(t)
  }

  ${
    StringBuilder sb = StringBuilder()
    sb.addString(String.fromCString(c"abc"))
    sb.addString(String.fromCString(c"def"))
    print(sb)
  }
}
