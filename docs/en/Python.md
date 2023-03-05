# Python Language

ScriptX and Python language type comparison table

|   Python   |  ScriptX   |
| :--------: | :--------: |
|    None    |    Null    |
|    dict    |   Object   |
|    list    |   Array    |
|   string   |   String   |
| int, float |   Number   |
|    bool    |  Boolean   |
|  function  |  Function  |
| bytearray  | ByteBuffer |

## Language specific implementation of Object

Unlike JavaScript and Lua, Python has an internal generic object base class Py_Object that cannot be instantiated (equivalent to an abstract base class), so it is not possible to fully equate the Object concepts of these two languages to Python.

Python's Object is currently implemented using `Py_Dict`, which is analogous to Lua's table.It is normal to set & get member properties and methods using `set` and `get`, and call member methods . But you can't use `Object::newObject` to call its constructor to construct a new object of the same type -- because they're both of type dict, and there's no constructor

## `eval` return value problem

The Python API provides two types of interfaces for executing code: the `eval` type can only execute a single expression and return its result, while the `exec` type provides support for executing multiple lines of code, which is the normal way of reading a file to execute code, but the return value is always `None`. This is due to the special design of the Python interpreter, which differs significantly from other languages.

Therefore, in the ScriptX implementation, if you use `Engine::eval` to execute a multi-line statement, the return value of `eval` will always be `Null`. If you need to get the return value, you can add an assignment line at the end of the executed code, and then use `Engine::get` to get the data of the result variable from the engine after `eval` finished.

## The weak reference problem of some built-in types

In CPython's design, some types in Python do not support weak references, for the following reason: [Why can't subclasses of tuple and str support weak references in Python? - Stack Overflow](https:// stackoverflow.com/questions/60213902/why-cant-subclasses-of-tuple-and-str-support-weak-references-in-python). The affected scope includes built-in types such as `int`, `str`, `tuple`, and certain other custom types that do not support weak references.

The current solution for this case is to use a strong reference implementation inside `Weak<>` that points to elements that do not support weak references. Therefore, when using `Weak<>` pointing to objects of these types, it may not be able to do exactly what Weak references are supposed to do (e.g. prevent circular references, prevent resources from being occupied all the time without GC, etc.), so please pay attention to this.

If you have any better solutions, please feel free to tell us.
