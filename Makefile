all: oss user

oss: oss.c
	gcc -lrt -o oss oss.c
	
user: user.c
	gcc -lrt -o user user.c
	
clean:
	rm oss user *.log
	