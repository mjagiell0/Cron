all: build-main

build-main:
	gcc -o main main.c cron_utils.c ../Logger/logger.c -pthread -lrt