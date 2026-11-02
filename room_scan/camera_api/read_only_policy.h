// Kernel-enforced policy for the manual camera prototype. No filesystem writes,
// camera setters, stream ownership, process signalling, module/boot operations,
// or outgoing connections. The API uses an ephemeral abstract Unix socket.
#pragma once
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/videodev2.h>
#include <linux/media.h>
#include <linux/dma-buf.h>
#include <linux/perf_event.h>

namespace room_camera_api {
inline void installReadOnlyPolicy(){
#if defined(__aarch64__)
    constexpr std::uint32_t architecture=AUDIT_ARCH_AARCH64;
#elif defined(__x86_64__)
    constexpr std::uint32_t architecture=AUDIT_ARCH_X86_64;
#else
#error Unsupported camera prototype architecture
#endif
    std::vector<sock_filter> code;
    const auto statement=[&](unsigned short instruction,std::uint32_t value){code.push_back(BPF_STMT(instruction,value));};
    const auto jump=[&](std::uint32_t value,unsigned char yes,unsigned char no){code.push_back(BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,value,yes,no));};
    statement(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,arch));jump(architecture,1,0);
    statement(BPF_RET|BPF_K,SECCOMP_RET_KILL_PROCESS);
    statement(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,nr));
#if defined(__x86_64__)
    code.push_back(BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K,0x40000000u,0,1));
    statement(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM);
#endif
    // openat/open are read-only even while temporarily running with sudo.
    const auto readOpen=[&](int call,unsigned argument){
        jump(std::uint32_t(call),0,5);
        statement(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,args)+argument*8);
        statement(BPF_ALU|BPF_AND|BPF_K,O_ACCMODE|O_CREAT|O_TRUNC|O_APPEND);
        jump(0,1,0);statement(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM);
        statement(BPF_RET|BPF_K,SECCOMP_RET_ALLOW);
    };
    readOpen(SYS_openat,2);
#ifdef SYS_open
    readOpen(SYS_open,1);
#endif
    // A tiny ioctl allowlist: read format/topology/queue metadata, configure our
    // own perf readers and perform READ cache synchronization on borrowed data.
    const std::uint32_t allowedIoctls[]={VIDIOC_QUERYCAP,VIDIOC_G_FMT,VIDIOC_QUERYBUF,
        MEDIA_IOC_G_TOPOLOGY,DMA_BUF_IOCTL_SYNC,PERF_EVENT_IOC_ENABLE,PERF_EVENT_IOC_DISABLE,PERF_EVENT_IOC_SET_OUTPUT};
    jump(SYS_ioctl,0,static_cast<unsigned char>(2+2*std::size(allowedIoctls)));
    statement(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,args)+8);
    for(auto request:allowedIoctls){jump(request,0,1);statement(BPF_RET|BPF_K,SECCOMP_RET_ALLOW);}
    statement(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM);
    for(int call:{SYS_write,SYS_writev}){
        jump(std::uint32_t(call),0,5);statement(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,args));
        jump(1,2,0);jump(2,1,0);statement(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM);
        statement(BPF_RET|BPF_K,SECCOMP_RET_ALLOW);
    }
    jump(SYS_socket,0,4);statement(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,args));
    jump(AF_UNIX,1,0);statement(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM);statement(BPF_RET|BPF_K,SECCOMP_RET_ALLOW);
    const int forbidden[]={
        SYS_pwrite64,SYS_pwritev,SYS_pwritev2,SYS_process_vm_writev,SYS_ptrace,SYS_kill,SYS_tkill,SYS_tgkill,
        SYS_pidfd_send_signal,SYS_rt_sigqueueinfo,SYS_rt_tgsigqueueinfo,SYS_prlimit64,SYS_process_madvise,
        SYS_connect,SYS_mount,SYS_umount2,SYS_pivot_root,SYS_reboot,SYS_kexec_load,
        SYS_init_module,SYS_finit_module,SYS_delete_module,SYS_bpf,SYS_swapon,SYS_swapoff,
        SYS_quotactl,SYS_acct,SYS_sethostname,SYS_setdomainname,SYS_openat2,
        SYS_renameat,SYS_renameat2,SYS_unlinkat,SYS_mkdirat,SYS_linkat,SYS_symlinkat,SYS_mknodat,
        SYS_fchmod,SYS_fchmodat,SYS_fchown,SYS_fchownat,SYS_truncate,SYS_ftruncate,
        SYS_fallocate,SYS_fcntl,SYS_settimeofday,SYS_clock_settime,SYS_clock_adjtime,SYS_adjtimex,
        SYS_setxattr,SYS_lsetxattr,SYS_fsetxattr,SYS_removexattr,SYS_lremovexattr,SYS_fremovexattr,
        SYS_utimensat,SYS_sendfile,SYS_copy_file_range,SYS_splice,SYS_vmsplice,
        SYS_io_uring_setup,SYS_io_uring_enter,SYS_io_uring_register,
        SYS_execve,SYS_execveat,SYS_clone,SYS_clone3,SYS_setns,SYS_unshare,SYS_chroot,
        SYS_open_by_handle_at,SYS_fsopen,SYS_fsconfig,SYS_fsmount,SYS_move_mount,SYS_open_tree,
#ifdef SYS_kexec_file_load
        SYS_kexec_file_load,
#endif
#ifdef SYS_creat
        SYS_creat,SYS_rename,SYS_unlink,SYS_mkdir,SYS_rmdir,SYS_link,SYS_symlink,SYS_mknod,SYS_chmod,SYS_chown,SYS_lchown,SYS_fork,SYS_vfork,
#endif
    };
    for(int call:forbidden){jump(std::uint32_t(call),0,1);statement(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM);}
    statement(BPF_RET|BPF_K,SECCOMP_RET_ALLOW);
    if(code.size()>4096)throw std::runtime_error("Camera syscall policy exceeds budget");
    sock_fprog program{static_cast<unsigned short>(code.size()),code.data()};
    if(prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)||prctl(PR_SET_SECCOMP,SECCOMP_MODE_FILTER,&program))
        throw std::runtime_error("Camera read-only syscall policy unavailable");
}
inline void sealReadOnlyMappings(){
    // Perf's writable consumer rings were mapped during setup. Future shared
    // writable maps and making existing read-only image maps writable are denied.
    std::vector<sock_filter> code;
    code.push_back(BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,nr)));
    code.push_back(BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,SYS_mmap,0,6));
    code.push_back(BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,args)+16));
    code.push_back(BPF_JUMP(BPF_JMP|BPF_JSET|BPF_K,PROT_WRITE,0,3));
    code.push_back(BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,args)+24));
    code.push_back(BPF_JUMP(BPF_JMP|BPF_JSET|BPF_K,MAP_SHARED,0,1));
    code.push_back(BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM));
    code.push_back(BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW));
    for(int call:{SYS_mprotect
#ifdef SYS_pkey_mprotect
        ,SYS_pkey_mprotect
#endif
    }){
        code.push_back(BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,std::uint32_t(call),0,4));
        code.push_back(BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(seccomp_data,args)+16));
        code.push_back(BPF_JUMP(BPF_JMP|BPF_JSET|BPF_K,PROT_WRITE,0,1));
        code.push_back(BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|EPERM));
        code.push_back(BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW));
    }
    code.push_back(BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW));
    sock_fprog program{static_cast<unsigned short>(code.size()),code.data()};
    if(prctl(PR_SET_SECCOMP,SECCOMP_MODE_FILTER,&program))throw std::runtime_error("Camera image map protection unavailable");
}
} // namespace room_camera_api
