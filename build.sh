rm -rf out

mkdir out

make O=out msm8226-perf_defconfig zc551kl-user-msm8226-perf_defconfig

make O=out -j8
