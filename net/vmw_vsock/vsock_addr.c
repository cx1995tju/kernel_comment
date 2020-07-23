/*
 * VMware vSockets Driver
 *
 * Copyright (C) 2007-2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation version 2 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/stddef.h>
#include <net/sock.h>
#include <net/vsock_addr.h>

void vsock_addr_init(struct sockaddr_vm *addr, u32 cid, u32 port)
{
	memset(addr, 0, sizeof(*addr)); //因为在通用缓冲区中动态分配的vsock_sock结构，所以addr指向的值不确定，所以要置零
	addr->svm_family = AF_VSOCK;
	addr->svm_cid = cid;
	addr->svm_port = port;
}
EXPORT_SYMBOL_GPL(vsock_addr_init);

int vsock_addr_validate(const struct sockaddr_vm *addr)
{
	if (!addr)
		return -EFAULT;

	if (addr->svm_family != AF_VSOCK)
		return -EAFNOSUPPORT;

	if (addr->svm_zero[0] != 0)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(vsock_addr_validate);

bool vsock_addr_bound(const struct sockaddr_vm *addr)
{
	return addr->svm_port != VMADDR_PORT_ANY; //初始化的时候赋予的是这个值，还是这个值表示没有绑定过
}
EXPORT_SYMBOL_GPL(vsock_addr_bound);

void vsock_addr_unbind(struct sockaddr_vm *addr)
{
	vsock_addr_init(addr, VMADDR_CID_ANY, VMADDR_PORT_ANY);
}
EXPORT_SYMBOL_GPL(vsock_addr_unbind);

bool vsock_addr_equals_addr(const struct sockaddr_vm *addr,
			    const struct sockaddr_vm *other)
{
	return addr->svm_cid == other->svm_cid &&
		addr->svm_port == other->svm_port;
}
EXPORT_SYMBOL_GPL(vsock_addr_equals_addr);

int vsock_addr_cast(const struct sockaddr *addr,
		    size_t len, struct sockaddr_vm **out_addr)
{
	if (len < sizeof(**out_addr))
		return -EFAULT;

	*out_addr = (struct sockaddr_vm *)addr;
	return vsock_addr_validate(*out_addr);
}
EXPORT_SYMBOL_GPL(vsock_addr_cast);
