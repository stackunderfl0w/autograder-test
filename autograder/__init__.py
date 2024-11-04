#!/usr/bin/env python3 
from pathlib import Path
import time
import subprocess
import signal
from ansi2html import Ansi2HTMLConverter
import shlex
import os

from typing import Any

class Autograder:
    _visibility = ['hidden', 'after_due_date', 'after_published', 'visible']
    _ofmt = ['text', 'html', 'simple_format', 'md', 'ansi']

    def __init__(self, *, score = None, exec_time = None,
                 output='', output_format = None,
                 test_output_format = None,
                 test_name_format=None, visibility=None,
                 stdout_visibility=None, extra_data = None,
                 tests=[], leaderboard=None):
        self.tests = tests
        self.score = score
        self.exec_time = exec_time
        self.output = output
        self.output_format = output_format
        self.test_name_format = test_name_format
        self.visibility = visibility
        self.stdout_visibility = stdout_visibility
        self.extra_data = extra_data
        self.leaderboard = leaderboard

        
    def __iter__(self):
        results = {}
        results['tests'] = []
        total_score = 0
        for test in self.tests:
            results['tests'] += test
            if test.points:
                total_score += test.score    
        results['score'] = total_score
        if self.score is not None:
            results['score'] = self.score
        for key, value in results.items():
            yield key, value

class AutograderTest:
    def __init__(self, *, name, name_format = 'html',
                 output = None, output_format = 'html',
                 visibility = None, points = None, score = None):
        super().__init__()
        self.name = name
        self.name_format = name_format
        self.output = output
        self.output_format = output_format
        self.visibility = visibility
        self.raw_points = points
        if score is not None:
            self.score = score
        else:
            self.failing = True # default to failing
    
    @property
    def name(self):
        return self._name

    @name.setter
    def name(self, name):
        if not name:
            raise ValueError('Name cannot be empty')
        self._name = str(name)

    @property
    def name_format(self):
        return self._name_format

    @name_format.setter
    def name_format(self, format):
        format = str(format) if format else None
        if format not in ['text', 'html', 'simple_format', 'md', 'ansi', None]:
            raise ValueError('Invalid format `{}\''.format(format))
        self._name_format = format

    @property
    def output(self):
        return self._output or ''

    @output.setter
    def output(self, s):
        self._output = str(s) if s else None

    @property
    def output_format(self):
        return self._output_format

    @output_format.setter
    def output_format(self, format):
        format = str(format) if format else None
        if format not in ['text', 'html', 'simple_format', 'md', 'ansi', None]:
            raise ValueError('Invalid format `{}\''.format(format))
        self._output_format = format

    @property
    def visibility(self):
        return self._visibility

    @visibility.setter
    def visibility(self, vis):
        vis = str(vis) if vis else None
        if vis not in ['hidden', 'after_due_date', 'after_published', 'visible', None]:
            raise ValueError('Invalid visibility setting `{}\''.format(vis))
        self._visibility = vis

    # raw_points and raw_score override boundary/range checks
    @property
    def raw_points(self):
        return self._points
 
    @raw_points.setter
    def raw_points(self, points):
        self._points = points
    
    @property
    def raw_score(self):
        return self._score

    @raw_score.setter
    def raw_score(self, raw_score):
        self._score = raw_score

    # points and score are range-checked so that score is always between 0 and points, inclusive
    # If points is falsish, score is boolish, so any value of score is allowed
    @property
    def points(self):
        return self.raw_points

    @points.setter
    def points(self, points):
        if points and not min(0, points) <= self.score <= max(0, points):
            raise ValueError('score {} not in range [{}, {}]'.format(self.score, min(0, points), max(0, points)))
        self.raw_points = points

    @property
    def score(self):
        return self.raw_score

    @score.setter
    def score(self, score):
        if self.points and not min(0, self.points) <= score <= max(0, self.points):
            raise ValueError('score {} not in range [{}, {}]'.format(score, min(0, self.points), max(0, self.points)))
        self.raw_score = score

    # Convenience methods
    # if points is falsish, score is interpreted as a boolish value
    # otherwise, passing if score == max(0, points)

    def __bool__(self):
        return self.passing

    @property
    def passing(self):
        if self.points:
            return (self.score == self.points) if (self.points > 0) else (self.score == 0)
        else:
            return bool(self.score)

    @passing.setter
    def passing(self, val):
        if val:
            if self.points:
                self.score = max(0, self.points)
            else:
                self.score = True
        else:
            self.failing = True

    @property
    def failing(self):
        return not self.passing

    @failing.setter
    def failing(self, val):
        if val:
            if self.points:
                self.score = min(0, self.points)
            else:
                self.score = False
        else:
            self.passing = True

    def __iter__(self):
        ret = {}
        name = self.name
        if self.name_format:
            ret['name_format'] = self.name_format
        if self.output:
            ret['output'] = self.output
        if self.output_format:
            ret['output_format'] = self.output_format

        if self.points:
            if self.points > 0:
                ret['max_score'] = self.points
            elif self.score < 0:
                name += ' ({})'.format(self.score)
            ret['score'] = self.score
        ret['name'] = name
        ret['status'] = 'passed' if self.passing else 'failed'
        yield ret

    def __repr__(self):
        return repr(list(self))

    def __str__(self):
        return repr(self)


class RunCommand:
    def __init__(self):
        self.crash_recorded = False

    def __call__(self, popen_args, *, name=None, timeout=5, input=None, **popen_kwargs):
        popen_kwargs.setdefault('stdin', subprocess.PIPE)
        popen_kwargs.setdefault('stdout', subprocess.PIPE)
        popen_kwargs.setdefault('stderr', subprocess.PIPE)
        popen_kwargs.setdefault('shell', True)
        popen_kwargs.setdefault('start_new_session', True)
        if 'user' in popen_kwargs and popen_kwargs['user']:
            status = os.system("useradd -m {} 2>/dev/null 1>/dev/null".format(popen_kwargs['user']))
            retcode = os.waitstatus_to_exitcode(status)
            if retcode not in [0, 9]:
                raise Exception("Failed to create user {}".format(popen_kwargs['user']))

        if popen_kwargs['shell']:
            args = 'exec ' + popen_args
        else:
            args = popen_args

        print("Running {}".format(str(args)))
        proc = subprocess.Popen(args, **popen_kwargs)
        returncode = None
        try:
            out, err = proc.communicate(timeout=timeout, input=input)
            returncode = proc.returncode
        except subprocess.TimeoutExpired:
            proc.kill()
            out, err = proc.communicate(timeout=1)

        if returncode == None or proc.returncode < 0:
            print("Crashed -- proc.returncode = {}".format(proc.returncode))
            self.crash_recorded = True

        class RunResult:
            def __init__(self, name, cmd, input, out, err, ret):
                self._name = name
                self._cmd = cmd
                self._in = input
                self._out = out
                self._err = err
                self._ret = ret
                self._to = timeout
            
            
            def html(self, title='command', stdin: Any=True, stdout: Any=True, stderr: Any=True, status: Any=True):
                str = ''
                str += rubric(title, bytes2html(self.cmd))
                for stream in "stdin", "stdout", "stderr":
                    if locals()[stream] not in [False, None]:
                        if locals()[stream] == True:
                            text = bytes2html(getattr(self, stream))
                        else:
                            text = locals()[stream]
                        str += rubric(stream, text)
                if status:
                    str += rubric('status', self.disposition)

                if self.name:
                    str = details(self.name, str)
                
                str = '<div style="border: 1px solid grey; padding: 5px; border-radius: 5px;">{}</div>'.format(str)
                return str
            
            @property
            def name(self):
                return self._name

            @property
            def disposition(self):
                if self._ret is None:
                    return 'Timed Out ({}s)'.format(self._to)
                elif self._ret < 0:
                    return 'Signaled ({})'.format(signal.Signals(-self._ret).name)
                else:
                    return 'Exited {}'.format(self._ret)
            
            @property
            def cmd(self):
                return self._cmd

            @property
            def stdin(self) -> bytes:
                return self._in

            @property
            def stdout(self) -> bytes:
                return self._out

            @property
            def stderr(self) -> bytes:
                return self._err
            
            @property
            def exited(self):
                return self._ret >= 0 if self._ret is not None else False

            @property
            def signaled(self):
                return self._ret < 0 if self._ret is not None else False

            @property
            def timedout(self):
                return self._ret is None

            @property
            def crashed(self):
                return self.signaled or self.timedout

            @property
            def returncode(self):
                return abs(self._ret) if self._ret is not None else None

        cmd = popen_args if isinstance(popen_args, str) else ' '.join([shlex.quote(arg) for arg in popen_args]) 
        result = RunResult(name, cmd, input, out, err, returncode)
        return result


def bytes2html(raw_bytes, *, empty='<p>(none)</p>', data_link_args={}, truncate=4096):
    if not raw_bytes:
        return empty
    
    truncated=False
    if truncate and len(raw_bytes) > truncate:
        truncated=True
        raw_bytes = raw_bytes[:truncate]

    if isinstance(raw_bytes, str):
        ascii=raw_bytes
    else:
        ascii=str(raw_bytes, encoding='ascii', errors='surrogateescape')

    converter = Ansi2HTMLConverter(inline=True, line_wrap=False)
    html_str = converter.convert(ascii, full=False)
    html_as_bytes = html_str.encode(encoding='ascii', errors='surrogateescape')
    pre_text = ''
    for byte in html_as_bytes:
        if 0x20 <= byte <= 0x7E or byte in [0x09, 0x0A]:
            pre_text += str(bytes([byte]), encoding='ascii')
        else:
            pre_text += ('<div style="line-height: normal;'
                                      'display: inline-block;'
                                      'outline: 1px solid rgba(255, 0, 0, 0.8);'
                                      'background: rgba(255, 0, 0, 0.1);'
                                      'margin: 0 -0.5ch 0 -0.5ch;'
                                      'padding: 0.25lh 0 0.25lh 0;'
                                      'transform: scale(0.5);">{:02X}</div>').format(byte)
    out =  ('<pre style="padding: 2px;'
            'border-radius: 2px;'
            'outline: 1px solid #cfcfcf;'
            'background: #f0f0f0;'
            'overflow: auto;'
            'min-height: 1lh;'
            'max-height: 10lh;'
            '">\n{}\n</pre>').format(pre_text)
    if truncated:
        out += '<div>[...Truncated...]</div>'
    return out


def rubric(title, contents):
    return '<div style="font-variant: small-caps; font-weight: bold;">{}</div><div>{}</div>'.format(title, contents)

def details(summary, contents):
    return '<details><summary>{}</summary>{}</details>'.format(summary, contents)
