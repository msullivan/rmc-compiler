#!/bin/bash

mv data/ archive/data-$(date -u --iso-8601=seconds)
mkdir data/
