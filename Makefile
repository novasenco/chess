
chess: chess.c
	clang -Ofast main.c chess.c -o chess

debug: chess.c
	clang -g main.c chess.c -o chess

bitboard: bitboard.c
	clang -Ofast -lncursesw -lX11 bitboard.c -o bitboard

.PHONY: clean
clean:
	rm -f chess bitboard
