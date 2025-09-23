clear
mkdir -p build
rm -rf build/*

# 配置 CMake 为 Debug 模式
cd build

cmake -DCMAKE_BUILD_TYPE=Debug ..

# 构建
make

# 安装
# make install