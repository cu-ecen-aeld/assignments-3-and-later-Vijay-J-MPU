# Kernel Crash Analysis - /dev/faulty

## Issue Description

While writing to the device:

```bash
echo "hello_world" > /dev/faulty
```

The system encountered a **kernel panic** due to a NULL pointer dereference.

---

##  Error Summary

> Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000  
> Internal error: Oops: 0000000096000045 [#1] SMP  

---

##  Key Observations

- Fault type: **Data Abort (DABT)**
- Exception Level: **Current EL**
- Fault Status Code: **Level 1 translation fault**
- Write operation caused crash (`WnR = 1`)
- Crash location:
  ```
  faulty_write+0x10/0x20 [faulty]
  ```

---

## Register Dump (Important Fields)

```text
x0 : 0000000000000000
x1 : 0000000000000000
x2 : 000000000000000c
x3 : ffffffc008ddbdc0
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
```

### Critical Insight

- `x0 = 0` → NULL pointer
- Kernel attempted to write using a NULL pointer

---

## Call Trace

```text
faulty_write+0x10/0x20 [faulty]
ksys_write+0x74/0x110
__arm64_sys_write+0x1c/0x30
invoke_syscall+0x54/0x130
el0_svc_common.constprop.0+0x44/0xf0
do_el0_svc+0x2c/0xc0
el0_svc+0x2c/0x90
el0t_64_sync_handler+0xf4/0x120
el0t_64_sync+0x18c/0x190
```

---

## Root Cause

The crash is caused by dereferencing a **NULL pointer inside `faulty_write()`**.

Example problematic pattern:

```c
char *ptr = NULL;
*ptr = 'A';   // causes kernel crash
```

---

##  Fix Recommendation

### Allocate Memory Properly

```c
char *ptr = kmalloc(10, GFP_KERNEL);
if (!ptr)
    return -ENOMEM;
```

---

### Validate Pointer Before Use

```c
if (!ptr)
    return -EINVAL;
```

---

### Safe User Buffer Handling

```c
if (copy_from_user(buffer, user_buf, count))
    return -EFAULT;
```

---

## Loaded Modules

```text
hello(O) faulty(O) scull(O)
```

---

## Conclusion

- Writing to `/dev/faulty` triggers a NULL pointer dereference
- Results in kernel OOPS during write operation
- Fix requires proper memory allocation and pointer validation

---
