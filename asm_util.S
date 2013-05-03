/**
* @file   asm_util.S
* @author Alex Podolsky <apodolsk@andrew.cmu.edu>
* 
* @brief  Assorted ASM utility code.
* 
*/

.global xadd
.global xchg
.global cmpxchg
.global cmpxchg8b
.global invlpg
.global hlt
.global cpuid

.global approx_esp

xadd:
    movl 4(%esp), %eax
    movl 8(%esp), %edx
        
    lock xadd %eax, (%edx)

    ret
    
xchg:   
    movl    4(%esp), %eax          # Get arg1: src
    movl    8(%esp), %ecx          # Get arg2: dest

    xchg    %eax, (%ecx)

    ret

cmpxchg:
    movl 4(%esp), %edx
    movl 8(%esp), %ecx
    movl 12(%esp), %eax

    lock cmpxchg %edx, (%ecx)
    ret

cmpxchg8b:
    pushl %ebx
    pushl %esi
    
    movl 12(%esp), %ebx
    movl 16(%esp), %ecx
    movl 20(%esp), %esi
    movl 24(%esp), %eax
    movl 28(%esp), %edx

    lock cmpxchg8b  (%esi)

    popl %esi
    popl %ebx
    ret

xor_locked:
    movl 4(%esp), %eax
    movl 8(%esp), %edx
    
    lock xor %eax, (%ecx)
    
    ret
    

invlpg:
    mov     4(%esp),%eax        # Get arg1.

    invlpg  (%eax)              # Invalidate *arg1

    ret
    
hlt:
    hlt
    ret

approx_esp:
    mov %esp, %eax
    ret

forward_call:
    movl 4(%esp), %eax
    movl (%esp), %ecx

    addl $8, %esp
    pushl %ecx

    jmp *%eax
    
cpuid:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi

    movl 8(%ebp), %eax
    movl 12(%ebp), %esi
    cpuid

    movl %eax, (%esi)
    movl %ebx, 4(%esi)
    movl %ecx, 8(%esi)
    movl %edx, 12(%esi)

    popl %esi
    popl %ebx
    leave
    ret