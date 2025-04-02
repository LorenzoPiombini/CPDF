TARGET := cpdf
SRC := $(wildcard src/*.c)
OBJ := $(patsubst src/%.c, obj/%.o, $(SRC))


default:$(TARGET)


clean:
	rm -f obj/*.o
	rm -f $(TARGET)


$(TARGET):$(OBJ)
	gcc -o $@ $? -lz -lm -fsanitize=address

obj/%.o:src/%.c
	gcc -Wall -Wextra -g3 -c $< -o $@ -Iinclude -fsanitize=address 
