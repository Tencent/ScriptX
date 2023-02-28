# Python语言

ScriptX和Python语言类型对照表

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

## Object 的语言特定实现

和 JavaScript 与 Lua 不同，Python 在内部通用的对象基类 Py_Object 无法被实例化（相当于抽象基类），因此无法将这两种语言中的 Object 概念完全等同到 Python 中。

目前 Python 的 Object 使用 `Py_Dict` 实现，类比于 Lua 的 table，同样可以使用 `set` `get` 设置成员属性和方法，并调用成员方法。但是无法使用 `Object::newObject` 调用其构造函数构造一个同类型的新对象 —— 因为它们的类型都是 dict，不存在构造函数

## `eval` 返回值问题

Python API 提供的执行代码接口分为两种：其中 eval 类型的接口只能执行单个表达式，并返回其结果；exec 类型的接口对执行多行代码提供支持（也就是正常读取文件执行代码所采取的方式），但是返回值恒定为`None`。这是由于 Python 解释器特殊的设计造成，与其他语言有较大差异。

因此，在ScriptX的实现中，如果使用 `Engine::eval`执行多行语句，则 `eval` 返回值一定为 `Null`。如果需要获取返回值，可以在所执行的代码最后添加一行赋值，并在 `eval` 执行完毕后使用 `Engine::get` 从引擎获取结果变量的数据。