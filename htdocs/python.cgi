#!/usr/bin/python
# -*- coding: UTF-8 -*-
# filename:test.py

import os
import sys

content_len = int(os.environ["CONTENT_LENGTH"])
req_body = sys.stdin.read(content_len)
print "req_body:" + req_body
print "<meta charset=\"utf-8\">"
print "<b>环境变量</b><br>"
print "<ul>"
for key in os.environ.keys():
    print "<li><span style='color:green'>%30s </span> : %s </li>" % (key,os.environ[key])
print "</ul>"