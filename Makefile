NAME = proj2
EXEC = $(NAME)
SOURCES = $(NAME).c

CXX = gcc
RM = rm -f

CFLAGS = -std=gnu99 -Wall -Wextra -Werror -pedantic
LDFLAGS = -lrt -pthread

.PHONY : all

all : $(EXEC)

$(EXEC) : $(SOURCES)
	$(CXX) $(SOURCES) $(CFLAGS) -o $(NAME) $(LDFLAGS)

clean:
	$(RM) *.out

cleanall: clean
	$(RM) $(EXEC)