#!/bin/bash

cd /home/buddhika/Builds/spec_cpu2017
source shrc
runcpu --config=gcc-test.cfg --size=test --label=STACK_PROTECTOR 619.lbm_s 625.x264_s 605.mcf_s 631.deepsjeng_s 657.xz_s
runcpu --config=gcc-test.cfg --size=test --label=SHADOW_STACK_LIGHT 619.lbm_s 625.x264_s 605.mcf_s 631.deepsjeng_s 657.xz_s
runcpu --config=gcc-test.cfg --size=test --label=SHADOW_STACK_FULL 619.lbm_s 625.x264_s 605.mcf_s 631.deepsjeng_s 657.xz_s
