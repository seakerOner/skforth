CC = gcc
FLAGS = -Wall -Wextra

BUILD = ./build/

skforth: 
	$(CC) $(FLAGS) main.c -o $(BUILD)skforth

run:
	make
	@echo " "
	$(BUILD)skforth

clear:
	rm -f $(BUILD)skforth
	
