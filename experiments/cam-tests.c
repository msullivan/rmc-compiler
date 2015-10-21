// typo in section 8 header in cppmem def

// Try 1
int main() {
  atomic_int clear = 0;
  int data = 0;

  {{{ { if (clear.load(mo_relaxed)) { data = 1; } }
  ||| { r2 = data;
        clear.store(1, mo_release); }
  }}};
  return 0;
}

// Try 2
int main() {
  atomic_int clear = 0;
  atomic_int data = 0;

  {{{ { if (clear.load(mo_relaxed)) { data.store(1, mo_relaxed); } }
  ||| { r2 = data.load(mo_relaxed).readsvalue(1);
        clear.store(1, mo_release); }
  }}};
  return 0;
}

// This one has a data dependency too --
// if we make the load mo_consumed it rules out the fail
int main() {
  atomic_int clear = 0;
  atomic_int data = 0;
  int thing;

  {{{ { thing = clear.load(mo_relaxed);
        if (thing) { data.store(thing, mo_relaxed); } }
  ||| { r2 = data.load(mo_relaxed).readsvalue(1);
        clear.store(1, mo_release); }
  }}};
  return 0;
}

// This is a weird test for a weird reason.
int main() {
  atomic_int x = 0;
  atomic_int y = 0;

  {{{ { x.store(1, mo_relaxed);
        x.store(2, mo_relaxed);
        y.store(1, mo_release); }
  ||| { r1 = y.load(mo_acquire).readsvalue(1);
        r2 = x.load(mo_relaxed); }
  }}};
  return 0;
}




#if 0

PPC MP+sync+ctrl
"SyncdWW Rfe DpCtrldR Fre"
Cycle=Rfe DpCtrldR Fre SyncdWW
{
0:r2=x; 0:r4=y;
1:r2=y; 1:r4=x;
}
 P0           | P1           ;
 li r1,1      | lwz r1,0(r2) ;
 stw r1,0(r2) | cmpw r1,r1   ;
 sync         | beq  LC00    ;
 li r3,1      | LC00:        ;
 stw r3,0(r4) | lwz r3,0(r4) ;
exists
(1:r1=1 /\ 1:r3=0)



PPC buf+sync+ctrl
"SyncdWW Rfe DpCtrldR Fre"
Cycle=Rfe DpCtrldR Fre SyncdWW
{
0:r2=c; 0:r4=d;
1:r2=c; 1:r4=d;
}
 P0           | P1           ;
 lwz r1,0(r2) | lwz r3,0(r4) ;
 cmpw r1,r1   | sync         ;
 beq LC00     | li r1,1      ;
 LC00:        | stw r1,0(r2) ;
 li r3,1      |              ;
 stw r3,0(r4) |              ;
exists
(0:r1=1 /\ 1:r3=1)


PPC buf+lwsync+ctrl
"SyncdWW Rfe DpCtrldR Fre"
Cycle=Rfe DpCtrldR Fre SyncdWW
{
0:r2=c; 0:r4=d;
1:r2=c; 1:r4=d;
}
 P0           | P1           ;
 lwz r1,0(r2) | lwz r3,0(r4) ;
 cmpw r1,r1   | lwsync       ;
 beq LC00     | li r1,1      ;
 LC00:        | stw r1,0(r2) ;
 li r3,1      |              ;
 stw r3,0(r4) |              ;
exists
(0:r1=1 /\ 1:r3=1)


ARM PPO002
"DMBdWW Rfe DpAddrdR PosRR DpAddrdR Fre"
Cycle=Rfe DpAddrdR PosRR DpAddrdR Fre DMBdWW
{
%x0=x; %y0=y;
%y1=y; %z1=z; %x1=x;
}
 P0            | P1               ;
 MOV R0, #1    | LDR R0, [%y1]    ;
 STR R0, [%x0] | EOR R1,R0,R0     ;
 DMB           | LDR R2, [R1,%z1] ;
 MOV R1, #1    | LDR R3, [%z1]    ;
 STR R1, [%y0] | EOR R4,R3,R3     ;
               | LDR R5, [R4,%x1] ;
exists
(1:R0=1 /\ 1:R5=0)

//Is I think what I want! - tragically, this does not work.

#endif

/////////////////////////////////////////////

// Out of thin air writes are really scary.
// Example adapted from http://dl.acm.org/citation.cfm?id=2618134 to
// work under http://svr-pes20-cppmem.cl.cam.ac.uk/cppmem/index.html

// Regular OOTA - can be fixed by making them release/acquire or rel/consume
int main() {
  atomic_int a, b;
  int l1, l2;

  a = 0; b = 0;

  {{{ { l1 = a.load(memory_order_relaxed).readsvalue(42);
        b.store(l1, memory_order_relaxed); }
  ||| { l2 = b.load(memory_order_relaxed);
        a.store(l2, memory_order_relaxed); } }}};
  return 0;
}



// Can wind up with an = &b, bn = &a!
int main() {
  int *p1, *p2;
  atomic_int a, an, b, bn;
  a = &an;
  b = &bn;

  {{{ { p1 = a.load(memory_order_relaxed);
        atomic_store_explicit(*p1, &a, memory_order_relaxed); }
  ||| { p2 = b.load(memory_order_relaxed);
        atomic_store_explicit(*p2, &b, memory_order_relaxed); } }}};
  return 0;
}

// But if there are no atomics, everything is fine!

int main() {
  int *p1, *p2;
  int a, an, b, bn;
  a = &an;
  b = &bn;

  {{{ { p1 = a;
        *p1 = &a; }
  ||| { p2 = b;
        *p2 = &b; } }}};
  return 0;
}


///////////////////////

// ok wait why can't SC fences save us?

// IRIW with fences
// the question is whether the reading threads have
// to see the writes to x and y in the same order.
// How about rel/acq plus SC fences
// Ok well it easts shit, huh
int main() {
  atomic_int x = 0; atomic_int y = 0;
  {{{ { x.store(1, memory_order_release);
        atomic_thread_fence(memory_order_seq_cst); }
  ||| { y.store(1, memory_order_release);
        atomic_thread_fence(memory_order_seq_cst); }
  ||| { r1=x.load(memory_order_acquire).readsvalue(1);
        atomic_thread_fence(memory_order_seq_cst);
        r2=y.load(memory_order_acquire).readsvalue(0);
        atomic_thread_fence(memory_order_seq_cst); }
  ||| { r3=y.load(memory_order_acquire).readsvalue(1);
        atomic_thread_fence(memory_order_seq_cst);
        r4=x.load(memory_order_acquire).readsvalue(0);
        atomic_thread_fence(memory_order_seq_cst); }
  }}};
  return 0; }

// Drop fences that shouldn't help
int main() {
  atomic_int x = 0; atomic_int y = 0;
  {{{ { x.store(1, memory_order_release);
                  /*atomic_thread_fence(memory_order_seq_cst);*/ }
  ||| { y.store(1, memory_order_release);
                  /*atomic_thread_fence(memory_order_seq_cst);*/ }
  ||| { r1=x.load(memory_order_acquire).readsvalue(1);
        atomic_thread_fence(memory_order_seq_cst);
        r2=y.load(memory_order_acquire).readsvalue(0);
        /*atomic_thread_fence(memory_order_seq_cst); */}
  ||| { r3=y.load(memory_order_acquire).readsvalue(1);
        atomic_thread_fence(memory_order_seq_cst);
        r4=x.load(memory_order_acquire).readsvalue(0);
        /*atomic_thread_fence(memory_order_seq_cst); */}
  }}};
  return 0; }

///////////////////////////////////////////////////////

// Test the WWP-READ case in C++
int main() {
  atomic_int x;
  {{{ x.store(1, memory_order_relaxed);
  ||| { r1=x.load(memory_order_relaxed).readsvalue(1);
        x.store(2, memory_order_relaxed); }
  ||| { r3=x.load(memory_order_relaxed).readsvalue(2);
        r4=x.load(memory_order_relaxed).readsvalue(1); }
  }}};
  return 0; }
