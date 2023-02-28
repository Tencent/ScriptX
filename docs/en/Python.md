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