CFLAGS += -I.
CFLAGS += -g

T_C = $(shell ls t/*.t.c)
T_T = $(T_C:%.c=%)

all: $(T_T)

$(T_T) : lispread.c

%.s : %.c
	$(CC) $(CFLAGS) -S -o $@ $(@:.s=.c)

test: $(T_T)
	@for t in $(T_T); do \
	  (echo "+ $$t" ; $$t < $$t.in; echo "exit($$?)") | tee $$t.out ;\
	done
	@error=0; for t in $(T_T); do \
	  diff -u $$t.exp $$t.out || error=1 ;\
	done ; exit $$error
	@echo test: OK

clean:
	rm -f $(GEN_H)
	rm -f src/*.o src/lib*.a t/*.t
	rm -rf t/*.dSYM
	rm -rf $(BIN_E) bin/*.dSYM

