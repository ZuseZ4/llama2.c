## Llama 2 Everywhere (L2E)

<p align="center">
  <img src="assets/llamas_everywhere.jpg" width="600" height="454" alt="LLamas Everywhere!">
</p>

Standalone, Binary Portable, Bootable Llama 2

The primary objective of Llama 2 Everywhere (L2E) is to ensure its compatibility across a wide range of devices, from booting on repurposed chromebooks discarded by school districts to high-density unikernel deployments in enterprises. 

We believe that in future by harnessing a legion of small specialized LLMs with modest hardware requirements which are networked, distributed, and self-coordinated, L2E has the potential to democratize access to AI and unlock collective intelligence that surpasses that of a single large LLM.

The current compelling use case of L2E involves training small models on diverse textual sources, including textbooks, open books, and comprehensive corpora like the SlimPajama corpus. These trained models can be deployed using L2E, enabling them to run as bootable instances on outdated school computers. This deployment scenario proves particularly valuable in school libraries or classrooms where internet connectivity is limited or unavailable, serving as an information gateway* for students without constant reliance on the internet.

By pursuing the vision of Llama 2 Everywhere, we aim to create an inclusive AI ecosystem that can adapt to diverse environments and empower individuals and communities on a global scale.

My research goal is to train models using various hardware telemetry data with the hope that the models learn to interpret sensor inputs and control actuators based on the insights they glean from the sensor inputs. This research direction may open up exciting possibilities in fields such as automation, space, robotics and IoT, where L2E can play a pivotal role in bridging the gap between AI and physical systems.


A friendly fork of the excellent [@karpathy's llama2.c](https://github.com/karpathy/llama2.c)

I will be mirrorring the progress of https://github.com/karpathy/llama2.c every week, add portability, performance improvements and convenience features such as a web interface which certainly would not fit in the upstream do to the minimalistic elegance requirements there.

### * How do we make sure that the output is factual and not hallucinated?

It's a chicken and egg problem. This has to be explored and figured out on the way. Some ideas on mind are:

1. Topic specialized models which are frequently updated maybe every month or two.
2. Fact Checking & Moderation specialized models which moderate or do fact checking on other model's output.
3. Reduce / mitigate hallucinations through output validation (both neural and rule based).
4. Prompt rewriting both neural and with rules.
5. Educators / Students / Users can flag answers. Administrators could update rules.


# Features

### NEW - Unikernel Build

Have you ever wanted to boot and inference a herd of 1000's of Virtual baby Llama 2 models on big ass enterprise servers? No? Well, now you can!

![l2e_unik](https://github.com/trholding/llama2.c/assets/93451215/415f00b4-25ed-4c30-b619-1c3404ababee)

Just do the following to build:

```bash
make run_unik_qemu_x86_64
```

Please note that the requirements - unikraft and musl sources - will automatically get cloned before building.

Once the build completes, (takes a while), run L2E like this:

```bash
qemu-system-x86_64 -m 256m -accel kvm -kernel build/L2E_qemu-x86_64
```

You can also run with -nographic option to directly interact in terminal.

```bash
qemu-system-x86_64 -m 256m -accel kvm -kernel build/L2E_qemu-x86_64 -nographic
```
Download and try this and the cosmocc build in the latest release.

## Portability Features

+ Single Executable that runs on any x86_64 OS (cosmocc builds)
- [x] GNU Linux 
- [x] GNU/Systemd
- [x] *BSD (NetBSD, OpenBSD, FreeBSD)
- [x] XNU's Not UNIX (Mac)
- [x] Bare Metal Boot (BIOS & EFI) (Not fully functional yet but almost...)
- [x] Windows
- [x] Runs on ARM64 via inbuilt BLINK emulation

+ Standalone
- [x] Embedded model and tokenizer via ZipOS (cosmocc), INCBIN, strliteral

Some combined features depend on a specific cosmocc toolchain: https://github.com/jart/cosmopolitan

Building this with gcc or clang would result in normal binaries similar to upstream.

Read more:
[How to build](https://github.com/trholding/llama2.c#portable-binary-build)

### Performance Features

**CPU**

- [x] OpenBLAS
- [x] CBLAS
- [x] BLIS
- [ ] Intel MKL (WIP)
- [ ] ArmPL (WIP)
- [ ] Apple Accelerate Framework (CBLAS) (WIP/Testing)

**CPU/GPU**

- [x] OpenMP 
- [x] OpenACC

Both OpenMP and OpenACC builds currently use host CPU and do not offload to GPU.

**GPU**

- [x] OpenCL (via CLBlast) (Direct - planned)
- [ ] OpenGL 
- [ ] Vulkan 
- [ ] CUDA 

Download the prebuilt run.com binary from releases

## llama2.c

<p align="left">
  <img src="assets/llama_cute.jpg" width="150" height="150" alt="Cute Llama">
</p>

A friendly fork of the excellent [llama2.c](https://github.com/karpathy/llama2.c)

The original repository offers a full-stack solution for training and inferring the Llama 2 LLM architecture using PyTorch and a simple 500-line C file. The focus is on minimalism and simplicity, and the repo is a young project that is still being actively developed. The author recommends looking at the TinyStories paper for inspiration, as small LLMs can have strong performance in narrow domains. The C inference engine in run.c was the main focus of the project, and the Llama 2 architecture is hard-coded with no dependencies.

## Feel the Magic

```bash
git clone https://github.com/trholding/llama2.c.git
cd llama2.c
make runfast
wget https://huggingface.co/karpathy/tinyllamas/resolve/main/stories15M.bin
./run stories15M.bin
```
You can also prompt the model with a prefix:

```bash
./run stories42M.bin -t 0.8 -n 256 -i "A big dog"
```

When prompting, the temperature and steps parameters are needed since we use simple positional arguments.

**Output**

> A big dog named Zip. He loved to run and play in the sun. He was a happy dog. One day, Zip saw a little bird on the ground. The bird looked helpless. Zip wanted to help the bird. He ran to the bird and lay down next to it. Zip and the bird became friends. They played together every day. Zip would carry the bird to play in the trees. The bird would fly around, and Zip would bark. They were very happy together.


## Models

The original author trained a series of small models on TinyStories, which took a few hours to train on their setup. The 110M model took around 24 hours. The models are hosted on huggingface hub:

| model | dim | n_layers | n_heads | max context length | parameters | val loss | download
| --- | --- | --- | --- | --- | --- | --- | --- |
| OG | 288 | 6 | 6 | 256 | 15M | 1.072 | [stories15M.bin](https://huggingface.co/karpathy/tinyllamas/resolve/main/stories15M.bin) |
| 42M| 512 | 8 | 8 | 1024 | 42M | 0.847 | [stories42M.bin](https://huggingface.co/karpathy/tinyllamas/resolve/main/stories42M.bin) |
| 110M| 768 | 12 | 12 | 1024 | 110M | 0.760 | [stories110M.bin](https://huggingface.co/karpathy/tinyllamas/resolve/main/stories110M.bin) |

The upstream project owner trained the llama2.c storyteller models on a 4X A100 40GB box provided by [Lambda labs](https://lambdalabs.com/service/gpu-cloud).

Quick note on sampling, the recommendation for good results is to use `-t 1.0 -p 0.9`, i.e. top-p sampling at 0.9 with temperature 1.0 (this is the default). To control the diversity of samples use either the temperature (i.e. vary `-t` between 0 and 1 and keep top-p off with `-p 0`) or the top-p value (i.e. vary `-p` between 0 and 1 and keep `-t 1`), but not both. Nice explainers on LLM sampling strategies include [this](https://peterchng.com/blog/2023/05/02/token-selection-strategies-top-k-top-p-and-temperature/), [this](https://docs.cohere.com/docs/controlling-generation-with-top-k-top-p) or [this](https://huggingface.co/blog/how-to-generate).

```bash
./run llama2_7b.bin
```
A converted **Meta's Llama 2 7b** model can be inferenced at a slow speed.

## Usage

**Full Usage**

```
Usage:   run <checkpoint> [options]
Example: ./run model.bin -n 256 -i "Once upon a time"
Options:
  -t <float>  temperature in [0,inf], default 1.0
  -p <float>  p value in top-p (nucleus) sampling in [0,1] default 0.9
  -s <int>    random seed, default time(NULL)
  -n <int>    number of steps to run for, default 256. 0 = max_seq_len
  -b <int>    number of tokens to buffer, default 1. 0 = max_seq_len
  -x <int>    extended info / stats, default 1 = on. 0 = off
  -i <string> input prompt
  -z <string> optional path to custom tokenizer
```
``<checkpoint>`` is the **mandatory** checkpoint / model file.

**Minimal Usage**

```bash
./run <checkpoint_file>
```

## Platforms

**Multi OS build**

`make run_cosmocc`

The binary will boot on baremetal and also run on any 64 Bit OS such as Linux, *BSD, Windows and slower on Aarch64 Mac & Linux.

Currently when used to boot, it won't be able to find the models. It's a toolchain feature with an anticipated PR merge.

The performance of this build is more than twice of the basic build.

The cosmopolitan toolchain is a requirement for this build to work. Read here: [How to build](https://github.com/trholding/llama2.c#portable-binary-build)

__Please note that the Multi OS binaries builds built with cosmocc cause a false positive with AV/Microsoft Defender and Virus Total__

The issue is that AV's consider unsigned binaries or binaries that contain multiple OS binary signatures in one binary as suspicious.
Get more insight here: https://github.com/trholding/llama2.c/issues/8 and https://github.com/jart/cosmopolitan/issues/342

**Linux**

Centos 7 / Amazon Linux 2018

`make rungnu` or `make runompgnu` to use openmp.

**Other Linux Distros / Mac**

`make runfast` or `make runomp` to use openmp.

**Windows**

Build on windows: 

`build_msvc.bat` in a Visual Studio Command Prompt 

The MSVC build will use openmp and max threads suitable for your CPU unless you set `OMP_NUM_THREADS` env.

Build on Linux and Windows:

`make win64` to use the mingw compiler toolchain.

**Android**

See this @lordunix's post on how to build this on Android within [termux](https://termux.dev/en/):

https://github.com/trholding/llama2.c/issues/7#issue-1867639275

TODO. 

## Performance

**Basic**

This build does not enable any optimizations.

```bash
make run
```
This can be used as baseline build against which performance of other builds can be compared.

**Fast**

This build enables basic performance boost with compiler provided optimizations.

```bash
make runfast
```
### Build wth Acceleration

**OpenMP**

This build enables acceleration via OpenMP

```bash
make run_cc_openmp
```

Requires [OpenMP](https://www.openmp.org/) libraries and compiler with OpenMP support to be available on system.
E.g. `apt install clang libomp-dev` on ubuntu

When you run inference make sure to use OpenMP flags to set the number of threads, e.g.:

```bash
OMP_NUM_THREADS=4 ./run out/model.bin
```
More threads is not always better.

**OpenACC**

This build enables acceleration via OpenACC

```bash
make run_cc_openacc
```

Requires [OpenACC](https://www.openacc.org/) libraries and compiler with OpenACC support to be available on system.

**OpenBLAS**

This build enables acceleration via OpenBLAS

```bash
make run_cc_openblas
```

Requires [OpenBLAS](https://github.com/xianyi/OpenBLAS) to be installed on system.

**BLIS**

This build enables acceleration via BLIS

```bash
make run_cc_blis
```
Requires [BLIS](https://github.com/flame/blis) compiled with `./configure --enable-cblas -t openmp,pthreads auto` to be installed on system.

**Intel oneAPI MKL**

This build enables acceleration via Intel® oneAPI Math Kernel Library on x86_64 systems and Intel Mac OS - WIP

```bash
make run_cc_mkl
```
Requires [Intel oneAPI MKL](https://www.intel.com/content/www/us/en/developer/tools/oneapi/onemkl.html) to be installed on system.

**Arm Performance Library (ArmPL)**

This build enables acceleration via Arm Performance Library on ARM64 systems such as Linux or Mac OS - WIP

```bash
make run_cc_armpl
```
Requires [ArmPL](https://developer.arm.com/Tools%20and%20Software/Arm%20Performance%20Libraries) to be installed on system.

**Apple Accelerate**

This build enables BLAS acceleration via Apple Accelerate on Mac OS - Testing

```bash
make run_cc_mac_accel
```
Requires [Apple Accelerate](https://developer.apple.com/accelerate/) to be available on system.

Note: Needs testing.

**Generic CBLAS**

This build enables acceleration with any Netlib CBLAS interface compatible libraries

```bash
make run_cc_cblas
```

Requires any BLAS library with Netlib CBLAS interface such as [LAPACK](https://www.netlib.org/lapack) to be installed on system.

**CLBlast (GPU/OpenCL)**

This build enables tuned GPU acceleration via OpenCL with CLBlast

```bash
make run_cc_clblast
```

Requires [CLBlast](https://github.com/CNugteren/CLBlast) compiled with `cmake -DNETLIB=ON` to be installed on system.

Note: Currently runs much slower than CPU! Requires investigation or memory I/O is a bottle neck on the test system.

## Portable Binary Build

Have you ever wanted to inference a baby Llama 2 model with a single executable on any OS or *as OS? No? Well, now you can!

By making use of the Cosmopolitan libc toolchain to build llama2.c we get the we can get those features.

Instructions

Get and build the comopolitan libc toolchain:

Follow instructions at https://github.com/jart/cosmopolitan

Or do:

```
sudo mkdir -p /opt
sudo chmod 1777 /opt
git clone https://github.com/jart/cosmopolitan /opt/cosmo
export PATH="/opt/cosmo/bin:/opt/cosmos/bin:$PATH"
echo 'PATH="/opt/cosmo/bin:/opt/cosmos/bin:$PATH"' >>~/.profile
cosmocc --update   # pull cosmo and build/rebuild toolchain
```

Example build to generate a Actually Portable Executable (APE) with embedded model:

```bash
mkdir out
wget https://huggingface.co/karpathy/tinyllamas/resolve/main/stories15M.bin -O out/model.bin
make run_cosmocc_incbin
```

Example build to generate a APE:

```bash
make run_cosmocc
```

Run or copy to any supported system and run:

If model is embedded:

```bash
./run.com
```

Else

```bash
/run.com model.bin
```

## All 'make' targets

Do make <target> to build for a particular target.

Example:

```bash
make run_cc_openmp
```

Targets:

NEW:

```
run_unik_qemu_x86_64 - Unikernel + embedded model Build (QEMU/x86_64)
```

```
run 			- Default build
rungnu 			- Generic linux distro build
runompgnu 		- Generic linux distro + OpenMP build 
runfast 		- Optimized build
run_cc_armpl 		- ARM PL BLAS accelerated build (ARM64) (WIP)
run_cc_blis 		- BLIS accelerated build
run_cc_cblas 		- Generic CBLAS accelerated build
run_cc_clblast 		- CLBlast OpenCL CBLAS GPU accelerated build
run_cc_mac_accel	- Mac OS CBLAS via Accelerate Framework (WIP/TEST)
run_cc_mkl 		- Intel MKL CBLAS build (x86_64 / intel Mac)(WIP)
run_cc_openacc		- OpenACC accelerated build
run_cc_openblas		- Openblas CBLAS accelerated build
run_cc_openmp		- OpenMP accelerated build
run_gcc_openmp_incbin	- Gcc + OpenMP + embedded model fast build
run_gcc_openmp_strlit	- Gcc + OpenMP + embedded model build
run_clang_openmp_incbin - Clang + OpenMP + embedded model fast build
run_clang_openmp_strlit	- Clang + OpenMP + embedded model build
run_gcc_static_incbin	- Static gcc + OpenMP + embedded model fast build
run_gcc_static_strlit	- Static gcc + OpenMP + embedded model build
run_clang_static_incbin - Static clang + OpenMP + embedded model fast build
run_clang_static_strlit - Static clang + OpenMP + embedded model build
run_cosmocc		- Portable + cosmocc
run_cosmocc_incbin	- Portable + cosmocc + embedded model fast build (All OSes)
run_cosmocc_strlit	- Portable + cosmocc + embedded model build (All OSes)
run_cosmocc_zipos	- Portable + cosmocc + embedded zip model build(All OSes)
```
All builds with embedded models need the model to be in ``out/`` directory and the model name has to be ``model.bin``

Example:

```bash
mkdir out
wget https://huggingface.co/karpathy/tinyllamas/resolve/main/stories15M.bin -O out/model.bin
```

## TODO

- [ ] Python Binding + Streamlit Demo (priority)
- [ ] Web UI + Minimal OpenAI API compat (priority)
- [ ] GNU/Linux kernel + efistub + cpio + l2e as init boot image (priority)
- [ ] Users need better docs / howto / example, especially VM related.
- [ ] Train a small test model on open books. (I need to figure out sourcing the compute)
- [x] Alt model embedding (_incbin, _strlit) (done)
- [x] CLI Chat - use any _incbin, _strlit or _zipos build. - Hacky prompt loop (done)
- [x] Clang builds (Makefile) (done)
- [x] Optimize OpenMP & OpenACC (done)
- [x] Unikraft unikernel Boot (WIP/Testing) (Task: Multi Arch + Firecracker VM support)
- [ ] Rename unikernel to Virtual Llama 2000 : Close to the Metal Part 1
- [ ] Intel MKL BLAS Acceleration (WIP)
- [ ] Arm Performance Libraries (WIP)
- [ ] Apple Accelerate BLAS (WIP/Testing)
- [ ] NetBSD Rump Kernel Boot (R&D, attempt failed, needs deep study)
- [ ] sgemm OpenGL acceleration (next)
- [ ] sgemm SSE, AVX acceleration
- [ ] Fix baremetal cosmo boot model loading (pending)
- [ ] OpenMP SIMD (pending)
- [ ] OpenCL pure
- [ ] Vulkan
- [ ] CUDA
- [ ] MPI / PVM / PBLAS
- [ ] cFS App
- [ ] Android support (both ndk builds and minimal APK)
- [ ] Various uC demos (ESP32, ESP8266, Pico) - load models via network, Raspi Zero Demo
- [ ] Quantization (16, 4 , 2)
- [ ] ~~CLara~~ (tried / broken) / SunCL Distributed OpenCL support
- [ ] Fix broken MSVC build (!) yikes
- [ ] Split, re-order, rebase repo.
 
## Changelog

See commits.

## Contributing

- All pull requests that are merged to upstream will be automatically applied here as we closely mirror upstream. 
- I merge pull requests that improves performance even if they are rejected upstream.
- Performance and usability improvement contriubutions are welcome.

## Developer Status

See "Developer Status" issue.

Current status: Busy since Aug ~6 2023, away on bigger IRL projects. Just merging stuff. Addressing all issues every ~7 days.

# Gratitude & Credits

Thank you to to the creators of the following libraries and tools and their contributors:

- [llama2.c](https://github.com/karpathy/llama2.c) - @karpathy
- [cosmopolitan](https://github.com/jart/cosmopolitan) - @jart
- [OpenBlas](https://github.com/xianyi/OpenBLAS) - @xianyi
- [blis](https://github.com/flame/blis) - @flame
- [CLBlast](https://github.com/CNugteren/CLBlast) - @CNugteren
- [incbin](https://github.com/graphitemaster/incbin) - @graphitemaster
- [strliteral](https://github.com/mortie/strliteral) - @mortie
- [unikraft](https://github.com/unikraft) - @unikraft


## Notable projects
- [llama.cpp](https://github.com/ggerganov/llama.cpp)
- [llama2.c](https://github.com/karpathy/llama2.c)
## License

MIT
