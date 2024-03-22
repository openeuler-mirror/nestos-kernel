# nestos-kernel

#### 介绍
It provides openEuler kernel source for Nestos

#### 使用说明

1.  Nestos源码仓是以openEuler kernel源码仓作为基础，同步欧拉支持的不同版本。
2.  通过内部讨论评估，选择合适的openEuler版本作为Nestos初始化版本。
3.  Nestos一个内核版本有两种模式的发布版本（NestOS-For-Virt和NestOS-For-Container)
    同时两种模式还会各自有一个用于版本迭代的NEXT分支。详情见Nestos的软件包仓库。
4.  Nestos源码仓master分支是5.10内核版本，后续同步欧拉6.6内核，会新建分支。

#### 参与贡献

1.  Fork 本仓库
2.  新建 Feat_xxx 分支
3.  提交代码
4.  新建 Pull Request

#### 版本维护策略

1. 每个版本以打标签（tag）的方式标记，按照标签区分不同版本。
2. 对应版本都会有一个[软件包仓库分支](https://gitee.com/src-openeuler/nestos-kernel/)，每个版本的软件包仓库分支会有一个SOURCE记录使用内核release版本，
   记录相应的标签（tag）情况。例如NestOS-For-Container_openEuler-22.03-LTS-SP3：[SOURCE](https://gitee.com/src-openeuler/nestos-kernel/blob/Multi-Version_NestOS-For-Container_openEuler-22.03-LTS-SP3/SOURCE)。
3. 对于release版本出现CVE等问题，回合相应的补丁到相应的版本中。

##### 版本同步：基于欧拉

1. 以版本初始化时的标签为base，每两个星期同步一次欧拉的源码仓库。以欧拉保持一致。
2. 同步时，应于欧拉保持一致，保持tag的一致。
3. 同步实施

> 使用脚本，批量拉取合并。
>
> 注意：由于存在自研特性导致代码的引入，合并冲突需要手动解决。

##### 版本同步：自研代码

1. 不同版本间的内核特性迁移：需评估才能决定是否在其他版本支持，评估后在进行迁移。
2. 添加版本新特性：根据当前版本release的情况，一般情况添加到最新的版本中,
   如该特性需要加入特定的release版本中，根据情况自定义添加。
3. 添加版本新特性：对于5.10.0内核版本的特性，如无特定版本合入，
   可以合入到maste分支，后续同步到其他特定需要的版本中
#### 特技

1.  使用 Readme\_XXX.md 来支持不同的语言，例如 Readme\_en.md, Readme\_zh.md
2.  Gitee 官方博客 [blog.gitee.com](https://blog.gitee.com)
3.  你可以 [https://gitee.com/explore](https://gitee.com/explore) 这个地址来了解 Gitee 上的优秀开源项目
4.  [GVP](https://gitee.com/gvp) 全称是 Gitee 最有价值开源项目，是综合评定出的优秀开源项目
5.  Gitee 官方提供的使用手册 [https://gitee.com/help](https://gitee.com/help)
6.  Gitee 封面人物是一档用来展示 Gitee 会员风采的栏目 [https://gitee.com/gitee-stars/](https://gitee.com/gitee-stars/)
