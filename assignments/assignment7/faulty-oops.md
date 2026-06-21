# Faulty driver oops

## The output

The output after issuing `“hello_world” > /dev/faulty` is as follows:

```
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x0000000096000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041c23000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 154 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
sp : ffffffc008dd3d20
x29: ffffffc008dd3d80 x28: ffffff8001b2a7c0 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 0000000000000012 x22: 0000000000000012 x21: ffffffc008dd3dc0
x20: 00000055787aa460 x19: ffffff8001b8b500 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc000787000 x3 : ffffffc008dd3dc0
x2 : 0000000000000012 x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110“hello_world” > /dev/faulty
 __arm64_sys_write+0x1c/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x2c/0xc0
 el0_svc+0x2c/0x90
 el0t_64_sync_handler+0xf4/0x120
 el0t_64_sync+0x18c/0x190
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 0000000000000000 ]---
```

## Analysis

1. Null pointer reference:
`Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000` <- kernel attempted to access address at 0.

2. Location of error - function faulty_write at 0x10:
`pc : faulty_write+0x10/0x20 [faulty]`

3. Objdump for function faulty_write:
```
0000000000000000 <faulty_write>:
   0:	d2800001 	mov	x1, #0x0                   	// #0
   4:	d2800000 	mov	x0, #0x0                   	// #0
   8:	d503233f 	paciasp
   c:	d50323bf 	autiasp
  10:	b900003f 	str	wzr, [x1]
  14:	d65f03c0 	ret
```

Issue is at: `10:	b900003f 	str	wzr, [x1]` <- Attempting to write 0 (str wzr) to x1 (second argument of the function).

faulty_write function is declared as:
`ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count, loff_t *pos)`

making the second argument: `const char __user *buf`, where an attempt was made to dereference address 0 in userspace (__user).
As PAN is turned off: `pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)`, the fault does not lie in attempted userspace access, instead it is due to NULL memory address: `FSC = 0x05: level 1 translation fault`.

However, as none of the arguments were actually utilized in the function:

```c
ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count, loff_t *pos)
{
	*(int *)0 = 0;
	return 0;
}
```

the compiler simply used the registeres x0 and x1, meant for the first two arguments, to store the 0 values (`x1 : 0000000000000000 x0 : 0000000000000000`) for the assignment operation: `*(int *)0 = 0;`. So regardless to what was passed to the module, it would crash in any case as it always attemps to assign 0 to non-existent address 0.

## Solution

In order to resolve the issue, the body of the function, and specifically the line `*(int *)0 = 0;`, needs to be removed and a proper block handling the write should be inserted. For example:

```c
#define MAX_BUFFER_SIZE 4096

ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count, loff_t *pos)
{
    char *text;

    // check if input size not larger than allowed; else throw error
    if (count > MAX_BUFFER_SIZE) {
        return -EMSGSIZE;
    }

    // allocate memory for text; else throw error
    text = kmalloc(count + 1, GFP_KERNEL);
    if (!text) {
        return -ENOMEM;
    }

    // copy user buffer to text; else free memory and throw error
    if (copy_from_user(text, buf, count)) {
        kfree(text);
        return -EFAULT;
    }

    // add trailing null to string
    text[count] = '\0';

    // output the text
    printk(KERN_INFO "Received from user: %s", text);

    // free allocated memory, shift position and return
    kfree(text);
    *pos += count;
    return count;
}
```
