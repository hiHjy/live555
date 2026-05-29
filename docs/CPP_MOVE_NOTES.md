# C++ std::move 学习记录

这份记录是结合当前 RTSP demo 里用到的 `std::move` 整理的。

## 1. std::move 本身不移动数据

`std::move(x)` 只是把 `x` 转成右值引用，让编译器知道：

```text
这个对象可以被移动了。
```

真正有没有发生移动，取决于对象类型有没有可用的移动构造函数或移动赋值函数。

可以理解成：

```text
std::move = 给移动创造条件
移动构造/移动赋值 = 真正搬资源
```

## 2. move 的三种结果

写了：

```cpp
T b = std::move(a);
```

结果可能有三种：

```text
T 有移动构造:
  调用移动构造

T 没有移动构造，但可以拷贝:
  退化成拷贝

T 不能移动也不能拷贝:
  编译失败
```

所以：

```text
std::move 不保证一定移动。
```

## 3. 结构体 move 时，会逐个成员 move

当前 demo 里有类似结构：

```cpp
struct Nalu {
    std::vector<uint8_t> data;
    timeval presentationTime;
};
```

当写：

```cpp
queue_.push_back(std::move(nalu));
```

如果没有手写移动构造，编译器会自动生成一个默认移动构造，大概等价于：

```cpp
Nalu(Nalu&& other)
    : data(std::move(other.data)),
      presentationTime(other.presentationTime)
{
}
```

也就是说：

```text
结构体的 move 会逐个成员 move。
```

其中：

- `std::vector<uint8_t>` 会调用自己的移动构造。
- `timeval` 这种普通结构体基本就是拷贝。

## 4. vector 的 move 有什么用

`std::vector` 内部通常有：

```text
buffer 指针
size
capacity
```

拷贝 vector 时：

```text
会重新分配内存，并复制所有元素。
```

移动 vector 时：

```text
通常只是把 buffer 指针、size、capacity 搬过去。
```

所以在 H264 队列里：

```cpp
Nalu nalu;
nalu.data.assign(data, data + size);
queue_.push_back(std::move(nalu));
```

含义是：

```text
MPP packet data -> nalu.data
  这里必须拷贝一次，因为 MPP packet 生命周期不归队列管理。

nalu.data -> queue_
  这里用 move，避免再拷贝一次 H264 数据。
```

## 5. shared_ptr 的 copy 和 move

`std::shared_ptr` 既能 copy，也能 move。

### copy shared_ptr

```cpp
std::shared_ptr<Foo> a = std::make_shared<Foo>();
std::shared_ptr<Foo> b = a;
```

结果：

```text
a 还指向 Foo
b 也指向 Foo
引用计数 +1
```

这是共享所有权。

### move shared_ptr

```cpp
std::shared_ptr<Foo> b = std::move(a);
```

结果：

```text
a 通常变空
b 指向原来的 Foo
引用计数通常不增加
```

所以：

```text
想共享，就 copy。
想转移这一份 shared_ptr 的持有权，才 move。
```

## 6. 当前 demo 里的安全 move

### NALU 入队

```cpp
queue_.push_back(std::move(nalu));
```

安全原因：

```text
nalu 是局部变量，push 进队列后不再使用。
```

这里 move 能避免 `vector<uint8_t>` 再复制一遍。

### 构造函数参数 move 到成员

```cpp
MyH264Subsession *MyH264Subsession::createNew(
    UsageEnvironment &env,
    std::shared_ptr<H264FrameQueue> frameQueue)
{
    return new MyH264Subsession(env, std::move(frameQueue));
}
```

这里 `frameQueue` 是函数参数，是外面 `frameQueue_` 拷贝出来的一份局部 `shared_ptr`。

调用点：

```cpp
sms->addSubsession(MyH264Subsession::createNew(*env_, frameQueue_));
```

这里没有写：

```cpp
std::move(frameQueue_)
```

所以不会把 `Live555RtspService::frameQueue_` 搬空。

完整过程：

```text
service::frameQueue_
  -> copy 给 createNew() 的参数 frameQueue
  -> 参数 frameQueue move 给 MyH264Subsession
  -> service::frameQueue_ 仍然有效
```

## 7. 什么时候不要随便 move

### 不要随便 move 成员变量

危险例子：

```cpp
foo(std::move(frameQueue_));
```

这会把当前对象自己的 `frameQueue_` 搬走。

除非你明确知道后面不再需要它，否则不要这样做。

### 不要 move 后继续依赖原内容

```cpp
std::string s = "hello";
std::string t = std::move(s);

// 不要再依赖 s 里还有 "hello"
```

move 后的对象仍然是合法对象，可以析构、重新赋值、clear，但不要依赖它原来的内容。

可以这样重新使用：

```cpp
s = "new value";
```

### 不要随便 move 长生命周期对象

例如：

```cpp
Live555RtspService
EncoderManager
CameraDevice
RtspSession
```

这类对象里可能有：

- 线程
- fd
- mutex
- 裸指针
- 回调注册关系
- 状态机

通常不应该随便 move。

更稳的做法是禁用 copy/move：

```cpp
class Live555RtspService {
public:
    Live555RtspService(const Live555RtspService&) = delete;
    Live555RtspService& operator=(const Live555RtspService&) = delete;
    Live555RtspService(Live555RtspService&&) = delete;
    Live555RtspService& operator=(Live555RtspService&&) = delete;
};
```

## 8. const 对象 move 可能退化成拷贝

例如：

```cpp
const std::string s = "hello";
std::string t = std::move(s);
```

这里 `std::move(s)` 得到的是：

```cpp
const std::string&&
```

而很多移动构造需要的是：

```cpp
std::string&&
```

不能修改源对象时，通常无法真正移动，所以可能走拷贝。

简单记：

```text
不要对 const 对象指望真正 move。
```

## 9. 安全使用 std::move 的判断

写 `std::move(x)` 前问自己：

```text
1. x 后面还需要原来的内容吗？
   需要：不要 move。
   不需要：可以考虑 move。

2. x 是局部临时变量，还是成员变量？
   局部临时变量：通常安全。
   成员变量：谨慎，可能把当前对象掏空。

3. x 是资源对象吗？
   vector/string/shared_ptr/unique_ptr 这类 move 有意义。
   int/timeval 这类 move 基本没意义。

4. x 是 const 吗？
   是：通常不要指望真正移动。
```

## 10. 一句话总结

```text
std::move 不是搬东西，而是告诉编译器“这个对象可以被搬”。

真正怎么搬，看类型自己的移动构造/移动赋值。

局部对象用完就丢，适合 move。
成员变量和长期对象，谨慎 move。
shared_ptr 想共享就 copy，想转移这一份持有权才 move。
```
