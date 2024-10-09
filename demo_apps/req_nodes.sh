#!/bin/sh

## assume will be run as: . ./script

# salloc --nodes=2 --gres=gpu:1 --time=01:00:00 --mem=50gb --cpus-per-task=8 --account=mathos --partition=pli

salloc --nodes=2 --gres=gpu:1 --time=01:00:00 --mem=100gb --cpus-per-task=8
