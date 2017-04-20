#!/bin/bash

make -C .. && make build/ms_queue-ermc-rmc-test && rm build/epoch_rmc.o build/ms_queue-ermc-rmc-test.o && time make build/ms_queue-ermc-rmc-test && make build/ms_queue-ec11-c11-test && rm build/epoch_c11.o build/ms_queue-ec11-c11-test.o && time make build/ms_queue-ec11-c11-test
