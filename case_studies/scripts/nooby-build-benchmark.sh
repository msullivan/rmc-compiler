#!/bin/bash

make -C .. && make build/ms_queue-ermc-rmc-test && rm build/epoch_rmc.o build/ms_queue-ermc-rmc-test.o && time make build/ms_queue-ermc-rmc-test
