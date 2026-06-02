# OS Workbench

操作系统课程实验，包含五个核心系统编程项目。

## 实验列表

| 实验 | 目录 | 说明 |
|------|------|------|
| CREPL | `crepl/` | C 语言 Read-Eval-Print Loop 交互式解释器 |
| GPT | `gpt/` | 通用线程池 / 线程同步机制 |
| libco | `libco/` | 用户态协程库实现 |
| pstree | `pstree/` | Linux `pstree` 命令实现（进程树可视化） |
| sperf | `sperf/` | 系统性能分析工具 |

## 构建

```bash
# 编译全部
make

# 单独编译
cd crepl && make
cd libco && make
cd pstree && make
cd sperf && make
```

## 环境

- Linux (WSL / 原生)
- GCC
- GNU Make
