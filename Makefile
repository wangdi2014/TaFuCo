all:
		$(CC) -g -O2 src/main.c src/name2fasta.c  src/predict.c src/kstring.c -o tafuco -lz  -lm