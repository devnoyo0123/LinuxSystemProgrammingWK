all:
	gcc -pthread -o qtrain qtrain.c
	gcc -o control_tower control_tower.c
clean:
	rm qtrain
	rm control_tower

