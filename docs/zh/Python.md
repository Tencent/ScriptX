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

## 部分内置类型的弱引用问题

在CPython的设计中，Python的部分类型并不支持弱引用，具体原因可见：[Why can't subclasses of tuple and str support weak references in Python? - Stack Overflow](https://stackoverflow.com/questions/60213902/why-cant-subclasses-of-tuple-and-str-support-weak-references-in-python)。受影响的范围包括`int`, `str`, `tuple`等内置类型，以及其他某些不支持弱引用的自定义类型。

对于这种情况，目前的解决方案是：指向不支持弱引用的元素的`Weak<>`内部使用强引用实现。因此在使用指向这些类型的对象的`Weak<>`时，可能无法完全起到Weak引用应有的作用（如防止循环引用、防止资源一直被占用无法GC等），请各位开发者留意。

如果有什么更好的解决方案欢迎提出。