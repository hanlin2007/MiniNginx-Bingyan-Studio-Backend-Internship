# Backend Internship Project of Bingyan Studio

**冰岩国庆后端实习项目MiniNginx简介**
Nginx是一个轻量级的Web服务器以及反向代理服务器，该代理服务实现两个主要功能：将请求转发到下游地址和静态文件服务器

阶段一：实现对http请求的反向代理以及对静态资源的管理
反向代理：服务器接收到请求后将请求转发，得到response后再回应给请求方
静态资源代理：静态代理服务依据请求文件，提取服务器中特定路径的静态文件，并返回给请求方

阶段二：为代理服务提供配置文件，以便随时设置和更改配置，提供：
server规则**
location规则**
master-woker**
配置文件实例**

阶段三：将阶段一和阶段二进行合成，让MiniNginx能够基于读取的配置文件“合理”地运行起来，可以实现master和worker进程
- 加入Epoll实现IO多路复用，至少支持接收每秒上千的访问量
- 实现配置热重载：配置文件发生改动后，自动重启MiniNginx更新操作

**工作日志**
2025.10.5晚上
真是艰难，到现在为止才好不容易配置好了所有工具aaa
从wsl，linux-ubuntu，VScode，到git，github，Cmake总算能够形成一条完整的日常工作链路了
学习手段：参考各种工具的配置和使用教程
感悟：在操作过程中真的会遇到很多无法提前预知的意外情况和复杂差错，而这些恰恰需要极强的耐心和细致去慢慢解决


2025.10.6下午
好开心，今天终于实现了全链开发工具的搭建，将项目结构和配置文件利用Cmake初始化
正式完成了第一个实用小服务器，能够响应http请求，处理端口监听和连接建立
日常开发流程梳理：
1.更改源代码
2.进行编译
#进入build目录
cd build
make
./MiniNginx
#终端占用，split另一个新的终端
curl http://localhost:8080
#推出build目录
cd ..
git add .
git commit -m"feat:"
git push origin main
pkill -f MiniNginx

补充：如果需要重新进行Cmake的构建
cd build
rm -rf *
cmake ..
make
./MiniNginx
学习手段：Ai工具反复追问和试错debug
感悟：最终一个项目的成功就是把遇到的问题一个个解决，真的实践中会遇到的错误太多太多了

另外感觉现在网上缺乏工具链路的搭建教程，新手小白确实完全不知道很多操作细节，容易产生很多错误
