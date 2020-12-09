#/********************************************************************************
# * Copyright (c) 2014, 2018 Cirrus Link Solutions and others
# *
# * This program and the accompanying materials are made available under the
# * terms of the Eclipse Public License 2.0 which is available at
# * http://www.eclipse.org/legal/epl-2.0.
# *
# * SPDX-License-Identifier: EPL-2.0
# *
# * Contributors:
# *   Cirrus Link Solutions - initial implementation
# ********************************************************************************/

LIBS = -Llib -L/usr/local/lib -lmosquitto
CC = gcc
CFLAGS = -g -Wall

.PHONY: clean client

client:
	$(CC) ../pb_common.c ../pb_decode.c ../pb_encode.c ../tahu.c ../tahu.pb.c \
		test_client.c -D__TEST_CLIENT__=1 -o led $(CFLAGS) $(LIBS)

clean:
	-rm -f cient
