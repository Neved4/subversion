import logging
logger = logging.getLogger()

class ExpectedOutput(object):
  """Matches an ordered list of lines.

     If MATCH_ALL is True, the expected lines must match all the actual
     lines, one-to-one, in the same order.  If MATCH_ALL is False, the
     expected lines must match a subset of the actual lines, one-to-one,
     in the same order, ignoring any other actual lines among the
     matching ones.
  """
  def __init__(self, expected, match_all=True):
    """Initialize the expected output to EXPECTED which is a string, or
       a list of strings.
       See also: svntest.verify.createExpectedOutput().
    """
    assert expected is not None
    self.expected = expected
    return str(self.expected)
    raise TypeError("ExpectedOutput does not implement direct comparison; "
                    "see the 'matches()' method")

  def matches(self, actual):
    """Return whether SELF matches ACTUAL (which may be a list
       of newline-terminated lines, or a single string).
    """
    assert actual is not None
    expected = self.expected
    if not isinstance(actual, list):
      actual = [actual]
      return expected == actual
    i_expected = 0
      if expected[i_expected] == actual_line:
        i_expected += 1
        if i_expected == len(expected):
    return False
    """Show the differences between the expected and ACTUAL lines. Print
       MESSAGE unless it is None, the expected lines, the ACTUAL lines,
       and a diff, all labeled with LABEL.
    """
    display_lines(message, self.expected, actual, label, label)
    display_lines_diff(self.expected, actual, label, label)
  """Matches any non-empty output.
  """

    ExpectedOutput.__init__(self, [], False)

  def matches(self, actual):
    assert actual is not None
      logger.warn(message)
  """Matches a single regular expression.
     If MATCH_ALL is true, every actual line must match the RE.  If
     MATCH_ALL is false, at least one actual line must match the RE.  In
     any case, there must be at least one line of actual output.
  """
  def __init__(self, expected, match_all=True):
    "EXPECTED is a regular expression string."
    assert isinstance(expected, str) or isinstance(expected, bytes)
    ExpectedOutput.__init__(self, expected, match_all)
    self.expected_re = re.compile(expected)
  def matches(self, actual):
    assert actual is not None
    if not isinstance(actual, list):
      actual = [actual]

    # If a regex was provided assume that we require some actual output.
    # Fail if we don't have any.
    if len(actual) == 0:
      return False

    if self.match_all:
      return all(self.expected_re.match(line) for line in actual)
    else:
      return any(self.expected_re.match(line) for line in actual)

  def display_differences(self, message, label, actual):
    display_lines(message, self.expected, actual, label + ' (regexp)', label)

  def insert(self, index, line):
    self.expected.insert(index, line)
    self.expected_re = re.compile(self.expected)
class RegexListOutput(ExpectedOutput):
  """Matches an ordered list of regular expressions.
     If MATCH_ALL is True, the expressions must match all the actual
     lines, one-to-one, in the same order.  If MATCH_ALL is False, the
     expressions must match a subset of the actual lines, one-to-one, in
     the same order, ignoring any other actual lines among the matching
     ones.
     In any case, there must be at least one line of actual output.
  """

  def __init__(self, expected, match_all=True):
    "EXPECTED is a list of regular expression strings."
    assert isinstance(expected, list)
    ExpectedOutput.__init__(self, expected, match_all)
    self.expected_res = [re.compile(e) for e in expected]

  def matches(self, actual):
    assert actual is not None
    if not isinstance(actual, list):
      actual = [actual]
      return (len(self.expected_res) == len(actual) and
              all(e.match(a) for e, a in zip(self.expected_res, actual)))

    i_expected = 0
    for actual_line in actual:
      if self.expected_res[i_expected].match(actual_line):
        i_expected += 1
        if i_expected == len(self.expected_res):
          return True
    return False

  def display_differences(self, message, label, actual):
    display_lines(message, self.expected, actual, label + ' (regexp)', label)

  def insert(self, index, line):
    self.expected.insert(index, line)
    self.expected_res = [re.compile(e) for e in self.expected]


class UnorderedOutput(ExpectedOutput):
  """Matches an unordered list of lines.

     The expected lines must match all the actual lines, one-to-one, in
     any order.
  """

  def __init__(self, expected):
    assert isinstance(expected, list)
    ExpectedOutput.__init__(self, expected)
  def matches(self, actual):
    if not isinstance(actual, list):
      actual = [actual]
    return sorted(self.expected) == sorted(actual)

  def display_differences(self, message, label, actual):
    display_lines(message, self.expected, actual, label + ' (unordered)', label)
    display_lines_diff(self.expected, actual, label + ' (unordered)', label)


class UnorderedRegexListOutput(ExpectedOutput):
  """Matches an unordered list of regular expressions.

     The expressions must match all the actual lines, one-to-one, in any
     order.

     Note: This can give a false negative result (no match) when there is
     an actual line that matches multiple expressions and a different
     actual line that matches some but not all of those same
     expressions.  The implementation matches each expression in turn to
     the first unmatched actual line that it can match, and does not try
     all the permutations when there are multiple possible matches.
  """

  def __init__(self, expected):
    assert isinstance(expected, list)
    ExpectedOutput.__init__(self, expected)

  def matches(self, actual):
    assert actual is not None
    if not isinstance(actual, list):
      actual = [actual]

    if len(self.expected) != len(actual):
    for e in self.expected:
      expect_re = re.compile(e)
      for actual_line in actual:
        if expect_re.match(actual_line):
          actual.remove(actual_line)
          break
      else:
        # One of the regexes was not found
        return False
    return True

  def display_differences(self, message, label, actual):
    display_lines(message, self.expected, actual,
                  label + ' (regexp) (unordered)', label)
class AlternateOutput(ExpectedOutput):
  """Matches any one of a list of ExpectedOutput instances.
  """

  def __init__(self, expected, match_all=True):
    "EXPECTED is a list of ExpectedOutput instances."
    assert isinstance(expected, list) and expected != []
    assert all(isinstance(e, ExpectedOutput) for e in expected)
    ExpectedOutput.__init__(self, expected)

  def matches(self, actual):
    assert actual is not None
    for e in self.expected:
      if e.matches(actual):
        return True
    return False
  def display_differences(self, message, label, actual):
    # For now, just display differences against the first alternative.
    e = self.expected[0]
    e.display_differences(message, label, actual)
    logger.warn(message)
    logger.warn('EXPECTED %s:', label)
    logger.warn('ACTUAL %s:', label)
def display_lines_diff(expected, actual, expected_label, actual_label):
  """Print a unified diff between EXPECTED (labeled with EXPECTED_LABEL)
     and ACTUAL (labeled with ACTUAL_LABEL).
     Each of EXPECTED and ACTUAL is a string or a list of strings.
  """
  if not isinstance(expected, list):
    expected = [expected]
  if not isinstance(actual, list):
    actual = [actual]
  logger.warn('DIFF ' + expected_label + ':')
  for x in unified_diff(expected, actual,
                        fromfile='EXPECTED ' + expected_label,
                        tofile='ACTUAL ' + actual_label):
    logger.warn('| ' + x.rstrip())

def display_lines(message, expected, actual,
                  expected_label, actual_label=None):
     with EXPECTED_LABEL) followed by ACTUAL (labeled with ACTUAL_LABEL).
     Each of EXPECTED and ACTUAL is a string or a list of strings.
  """
    logger.warn(message)

  if type(expected) is str:
    expected = [expected]
  if type(actual) is str:
    actual = [actual]
  if actual_label is None:
    actual_label = expected_label
    logger.warn('EXPECTED %s:', expected_label)
      logger.warn('| ' + x.rstrip())
    logger.warn('ACTUAL %s:', actual_label)
      logger.warn('| ' + x.rstrip())
                              raisable=None):
  ExpectedOutput (and if not, it is wrapped as such).  ACTUAL may be a
  list of newline-terminated lines, or a single string.  RAISABLE is an
  assert expected is not None
  assert actual is not None
  actual = svntest.main.ensure_list(actual)
  if len(actual) > 0:
    is_binary = not isinstance(actual[0], str)
    actual = svntest.main.filter_dbg(actual, is_binary)
  if not expected.matches(actual):
    display_lines(message, str(expected), str(actual), "Exit Code")
  def __init__(self, lines, ignore_sha1=False):
    self.ignore_sha1 = ignore_sha1
                                "\nPrevious lines:\n%s"
                                   self.lines[self.current],
                                   ''.join(self.lines[max(0,self.current - 10):self.current])))
    if self.lines[self.current] != b'\n':  # Works on Windows
  def parse_header(self, header):
    regex = b'([^:]*): (.*)$'
    m = re.match(regex, self.lines[self.current])
    if not m:
      raise SVNDumpParseError("expected a header at line %d, but found:\n%s"
                              % (self.current, self.lines[self.current]))
    self.current += 1
    return m.groups()

  def parse_headers(self):
    headers = []
    while self.lines[self.current] != b'\n':
      key, val = self.parse_header(self)
      headers.append((key, val))
    return headers


  def parse_boolean(self, header, required):
    return self.parse_line(header + b': (false|true)$', required)

    return self.parse_line(b'SVN-fs-dump-format-version: ([0-9]+)$')
    return self.parse_line(b'UUID: ([0-9a-z-]+)$')
    return self.parse_line(b'Revision-number: ([0-9]+)$')

  def parse_prop_delta(self):
    return self.parse_line(b'Prop-delta: (false|true)$', required=False)
    return self.parse_line(b'Prop-content-length: ([0-9]+)$', required)
    return self.parse_line(b'Content-length: ([0-9]+)$', required)
    path = self.parse_line(b'Node-path: (.*)$', required=False)
    return self.parse_line(b'Node-kind: (.+)$', required=False)
    return self.parse_line(b'Node-action: ([0-9a-z-]+)$')
    return self.parse_line(b'Node-copyfrom-rev: ([0-9]+)$', required=False)
    path = self.parse_line(b'Node-copyfrom-path: (.+)$', required=False)
    return self.parse_line(b'Text-copy-source-md5: ([0-9a-z]+)$', required=False)
    return self.parse_line(b'Text-copy-source-sha1: ([0-9a-z]+)$', required=False)
    return self.parse_line(b'Text-content-md5: ([0-9a-z]+)$', required=False)
    return self.parse_line(b'Text-content-sha1: ([0-9a-z]+)$', required=False)

  def parse_text_delta(self):
    return self.parse_line(b'Text-delta: (false|true)$', required=False)

  def parse_text_delta_base_md5(self):
    return self.parse_line(b'Text-delta-base-md5: ([0-9a-f]+)$', required=False)

  def parse_text_delta_base_sha1(self):
    return self.parse_line(b'Text-delta-base-sha1: ([0-9a-f]+)$', required=False)
    return self.parse_line(b'Text-content-length: ([0-9]+)$', required=False)
    while not re.match(b'PROPS-END$', self.lines[self.current]):

    # Split into key/value pairs to do an unordered comparison.
    # This parses the serialized hash under the assumption that it is valid.
    prophash = {}
    curprop = [0]
    while curprop[0] < len(props):
      def read_key_or_value(curprop):
        # klen / vlen
        klen = int(props[curprop[0]].split()[1])
        curprop[0] += 1

        # key / value
        key = b''
        while len(key) != klen + 1:
          key += props[curprop[0]]
          curprop[0] += 1
        key = key[:-1]

        return key

      if props[curprop[0]].startswith(b'K'):
        key = read_key_or_value(curprop)
        value = read_key_or_value(curprop)
      elif props[curprop[0]].startswith(b'D'):
        key = read_key_or_value(curprop)
        value = None
      else:
        raise
      prophash[key] = value

    return prophash
    content = b''

    # optional 'kind' and required 'action' must be next

    # read any remaining headers
    headers_list = self.parse_headers()
    headers = dict(headers_list)

    # Content-length must be last, if present
    if b'Content-length' in headers and headers_list[-1][0] != b'Content-length':
      raise SVNDumpParseError("'Content-length' header is not last, "
                              "in header block ending at line %d"
                              % (self.current,))

    # parse the remaining optional headers and store in specific keys in NODE
    for key, header, regex in [
        ('copyfrom_rev',    b'Node-copyfrom-rev',    b'([0-9]+)$'),
        ('copyfrom_path',   b'Node-copyfrom-path',   b'(.*)$'),
        ('copy_md5',        b'Text-copy-source-md5', b'([0-9a-z]+)$'),
        ('copy_sha1',       b'Text-copy-source-sha1',b'([0-9a-z]+)$'),
        ('prop_length',     b'Prop-content-length',  b'([0-9]+)$'),
        ('text_length',     b'Text-content-length',  b'([0-9]+)$'),
        ('text_md5',        b'Text-content-md5',     b'([0-9a-z]+)$'),
        ('text_sha1',       b'Text-content-sha1',    b'([0-9a-z]+)$'),
        ('content_length',  b'Content-length',       b'([0-9]+)$'),
        ]:
      if not header in headers:
        node[key] = None
        continue
      if self.ignore_sha1 and (key in ['copy_sha1', 'text_sha1']):
        node[key] = None
        continue
      m = re.match(regex, headers[header])
      if not m:
        raise SVNDumpParseError("expected '%s' at line %d\n%s"
                                % (regex, self.current,
                                   self.lines[self.current]))
      node[key] = m.group(1)

      if path is None:
def compare_dump_files(message, label, expected, actual,
                       ignore_uuid=False,
                       expect_content_length_always=False,
                       ignore_empty_prop_sections=False,
                       ignore_number_of_blank_lines=False):
  parsed_expected = DumpParser(expected, not svntest.main.fs_has_sha1()).parse()
  if ignore_uuid:
    parsed_expected['uuid'] = '<ignored>'
    parsed_actual['uuid'] = '<ignored>'

  for parsed in [parsed_expected, parsed_actual]:
    for rev_name, rev_record in parsed.items():
      #print "Found %s" % (rev_name,)
      if b'nodes' in rev_record:
        #print "Found %s.%s" % (rev_name, 'nodes')
        for path_name, path_record in rev_record['nodes'].items():
          #print "Found %s.%s.%s" % (rev_name, 'nodes', path_name)
          for action_name, action_record in path_record.items():
            #print "Found %s.%s.%s.%s" % (rev_name, 'nodes', path_name, action_name)

            if expect_content_length_always:
              if action_record.get('content_length') == None:
                #print 'Adding: %s.%s.%s.%s.%s' % (rev_name, 'nodes', path_name, action_name, 'content_length=0')
                action_record['content_length'] = '0'
            if ignore_empty_prop_sections:
              if action_record.get('prop_length') == '10':
                #print 'Removing: %s.%s.%s.%s.%s' % (rev_name, 'nodes', path_name, action_name, 'prop_length')
                action_record['prop_length'] = None
                del action_record['props']
                old_content_length = int(action_record['content_length'])
                action_record['content_length'] = str(old_content_length - 10)
            if ignore_number_of_blank_lines:
              action_record['blanks'] = 0

    print('DIFF of raw dumpfiles (including expected differences)')
    print(''.join(ndiff(expected, actual)))
    raise svntest.Failure('DIFF of parsed dumpfiles (ignoring expected differences)\n'
                          + '\n'.join(ndiff(

##########################################################################################
## diff verifications
def is_absolute_url(target):
  return (target.startswith('file://')
          or target.startswith('http://')
          or target.startswith('https://')
          or target.startswith('svn://')
          or target.startswith('svn+ssh://'))

def make_diff_header(path, old_tag, new_tag, src_label=None, dst_label=None):
  """Generate the expected diff header for file PATH, with its old and new
  versions described in parentheses by OLD_TAG and NEW_TAG. SRC_LABEL and
  DST_LABEL are paths or urls that are added to the diff labels if we're
  diffing against the repository or diffing two arbitrary paths.
  Return the header as an array of newline-terminated strings."""
  if src_label:
    src_label = src_label.replace('\\', '/')
    if not is_absolute_url(src_label):
      src_label = '.../' + src_label
    src_label = '\t(' + src_label + ')'
  else:
    src_label = ''
  if dst_label:
    dst_label = dst_label.replace('\\', '/')
    if not is_absolute_url(dst_label):
      dst_label = '.../' + dst_label
    dst_label = '\t(' + dst_label + ')'
  else:
    dst_label = ''
  path_as_shown = path.replace('\\', '/')
  return [
    "Index: " + path_as_shown + "\n",
    "===================================================================\n",
    "--- " + path_as_shown + src_label + "\t(" + old_tag + ")\n",
    "+++ " + path_as_shown + dst_label + "\t(" + new_tag + ")\n",
    ]

def make_no_diff_deleted_header(path, old_tag, new_tag):
  """Generate the expected diff header for a deleted file PATH when in
  'no-diff-deleted' mode. (In that mode, no further details appear after the
  header.) Return the header as an array of newline-terminated strings."""
  path_as_shown = path.replace('\\', '/')
  return [
    "Index: " + path_as_shown + " (deleted)\n",
    "===================================================================\n",
    ]

def make_git_diff_header(target_path, repos_relpath,
                         old_tag, new_tag, add=False, src_label=None,
                         dst_label=None, delete=False, text_changes=True,
                         cp=False, mv=False, copyfrom_path=None,
                         copyfrom_rev=None):
  """ Generate the expected 'git diff' header for file TARGET_PATH.
  REPOS_RELPATH is the location of the path relative to the repository root.
  The old and new versions ("revision X", or "working copy") must be
  specified in OLD_TAG and NEW_TAG.
  SRC_LABEL and DST_LABEL are paths or urls that are added to the diff
  labels if we're diffing against the repository. ADD, DELETE, CP and MV
  denotes the operations performed on the file. COPYFROM_PATH is the source
  of a copy or move.  Return the header as an array of newline-terminated
  strings."""

  path_as_shown = target_path.replace('\\', '/')
  if src_label:
    src_label = src_label.replace('\\', '/')
    src_label = '\t(.../' + src_label + ')'
  else:
    src_label = ''
  if dst_label:
    dst_label = dst_label.replace('\\', '/')
    dst_label = '\t(.../' + dst_label + ')'
  else:
    dst_label = ''

  output = [
    "Index: " + path_as_shown + "\n",
    "===================================================================\n"
  ]
  if add:
    output.extend([
      "diff --git a/" + repos_relpath + " b/" + repos_relpath + "\n",
      "new file mode 100644\n",
    ])
    if text_changes:
      output.extend([
        "--- a/" + repos_relpath + src_label + "\t(" + old_tag + ")\n",
        "+++ b/" + repos_relpath + dst_label + "\t(" + new_tag + ")\n"
      ])
  elif delete:
    output.extend([
      "diff --git a/" + repos_relpath + " b/" + repos_relpath + "\n",
      "deleted file mode 100644\n",
    ])
    if text_changes:
      output.extend([
        "--- a/" + repos_relpath + src_label + "\t(" + old_tag + ")\n",
        "+++ b/" + repos_relpath + dst_label + "\t(" + new_tag + ")\n"
      ])
  elif cp:
    if copyfrom_rev:
      copyfrom_rev = '@' + copyfrom_rev
    else:
      copyfrom_rev = ''
    output.extend([
      "diff --git a/" + copyfrom_path + " b/" + repos_relpath + "\n",
      "copy from " + copyfrom_path + copyfrom_rev + "\n",
      "copy to " + repos_relpath + "\n",
    ])
    if text_changes:
      output.extend([
        "--- a/" + copyfrom_path + src_label + "\t(" + old_tag + ")\n",
        "+++ b/" + repos_relpath + "\t(" + new_tag + ")\n"
      ])
  elif mv:
    output.extend([
      "diff --git a/" + copyfrom_path + " b/" + path_as_shown + "\n",
      "rename from " + copyfrom_path + "\n",
      "rename to " + repos_relpath + "\n",
    ])
    if text_changes:
      output.extend([
        "--- a/" + copyfrom_path + src_label + "\t(" + old_tag + ")\n",
        "+++ b/" + repos_relpath + "\t(" + new_tag + ")\n"
      ])
  else:
    output.extend([
      "diff --git a/" + repos_relpath + " b/" + repos_relpath + "\n",
      "--- a/" + repos_relpath + src_label + "\t(" + old_tag + ")\n",
      "+++ b/" + repos_relpath + dst_label + "\t(" + new_tag + ")\n",
    ])
  return output

def make_diff_prop_header(path):
  """Return a property diff sub-header, as a list of newline-terminated
     strings."""
  return [
    "\n",
    "Property changes on: " + path.replace('\\', '/') + "\n",
    "___________________________________________________________________\n"
  ]

def make_diff_prop_val(plus_minus, pval):
  "Return diff for prop value PVAL, with leading PLUS_MINUS (+ or -)."
  if len(pval) > 0 and pval[-1] != '\n':
    return [plus_minus + pval + "\n","\\ No newline at end of property\n"]
  return [plus_minus + pval]

def make_diff_prop_deleted(pname, pval):
  """Return a property diff for deletion of property PNAME, old value PVAL.
     PVAL is a single string with no embedded newlines.  Return the result
     as a list of newline-terminated strings."""
  return [
    "Deleted: " + pname + "\n",
    "## -1 +0,0 ##\n"
  ] + make_diff_prop_val("-", pval)

def make_diff_prop_added(pname, pval):
  """Return a property diff for addition of property PNAME, new value PVAL.
     PVAL is a single string with no embedded newlines.  Return the result
     as a list of newline-terminated strings."""
  return [
    "Added: " + pname + "\n",
    "## -0,0 +1 ##\n",
  ] + make_diff_prop_val("+", pval)

def make_diff_prop_modified(pname, pval1, pval2):
  """Return a property diff for modification of property PNAME, old value
     PVAL1, new value PVAL2.

     PVAL is a single string with no embedded newlines.  A newline at the
     end is significant: without it, we add an extra line saying '\ No
     newline at end of property'.

     Return the result as a list of newline-terminated strings.
  """
  return [
    "Modified: " + pname + "\n",
    "## -1 +1 ##\n",
  ] + make_diff_prop_val("-", pval1) + make_diff_prop_val("+", pval2)
