#include <stddef.h>
#include "svn_hash.h"
#include "svn_ctype.h"
#include "svn_mergeinfo.h"
#include "private/svn_diff_private.h"
#include "private/svn_sorts_private.h"

#include "diff.h"

#include "svn_private_config.h"
  const svn_patch_t *patch;

  /* Did we see a 'file does not end with eol' marker in this hunk? */
  svn_boolean_t original_no_final_eol;
  svn_boolean_t modified_no_final_eol;

  /* Fuzz penalty, triggered by bad patch targets */
  svn_linenum_t original_fuzz;
  svn_linenum_t modified_fuzz;
};

struct svn_diff_binary_patch_t {
  /* The patch this hunk belongs to. */
  const svn_patch_t *patch;

  /* APR file handle to the patch file this hunk came from. */
  apr_file_t *apr_file;

  /* Offsets inside APR_FILE representing the location of the patch */
  apr_off_t src_start;
  apr_off_t src_end;
  svn_filesize_t src_filesize; /* Expanded/final size */

  /* Offsets inside APR_FILE representing the location of the patch */
  apr_off_t dst_start;
  apr_off_t dst_end;
  svn_filesize_t dst_filesize; /* Expanded/final size */
/* Common guts of svn_diff_hunk__create_adds_single_line() and
 * svn_diff_hunk__create_deletes_single_line().
 *
 * ADD is TRUE if adding and FALSE if deleting.
 */
static svn_error_t *
add_or_delete_single_line(svn_diff_hunk_t **hunk_out,
                          const char *line,
                          const svn_patch_t *patch,
                          svn_boolean_t add,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_diff_hunk_t *hunk = apr_pcalloc(result_pool, sizeof(*hunk));
  static const char *hunk_header[] = { "@@ -1 +0,0 @@\n", "@@ -0,0 +1 @@\n" };
  const apr_size_t header_len = strlen(hunk_header[add]);
  const apr_size_t len = strlen(line);
  const apr_size_t end = header_len + (1 + len); /* The +1 is for the \n. */
  svn_stringbuf_t *buf = svn_stringbuf_create_ensure(end + 1, scratch_pool);

  hunk->patch = patch;

  /* hunk->apr_file is created below. */

  hunk->diff_text_range.start = header_len;
  hunk->diff_text_range.current = header_len;

  if (add)
    {
      hunk->original_text_range.start = 0; /* There's no "original" text. */
      hunk->original_text_range.current = 0;
      hunk->original_text_range.end = 0;
      hunk->original_no_final_eol = FALSE;

      hunk->modified_text_range.start = header_len;
      hunk->modified_text_range.current = header_len;
      hunk->modified_text_range.end = end;
      hunk->modified_no_final_eol = TRUE;

      hunk->original_start = 0;
      hunk->original_length = 0;

      hunk->modified_start = 1;
      hunk->modified_length = 1;
    }
  else /* delete */
    {
      hunk->original_text_range.start = header_len;
      hunk->original_text_range.current = header_len;
      hunk->original_text_range.end = end;
      hunk->original_no_final_eol = TRUE;

      hunk->modified_text_range.start = 0; /* There's no "original" text. */
      hunk->modified_text_range.current = 0;
      hunk->modified_text_range.end = 0;
      hunk->modified_no_final_eol = FALSE;

      hunk->original_start = 1;
      hunk->original_length = 1;

      hunk->modified_start = 0;
      hunk->modified_length = 0; /* setting to '1' works too */
    }

  hunk->leading_context = 0;
  hunk->trailing_context = 0;

  /* Create APR_FILE and put just a hunk in it (without a diff header).
   * Save the offset of the last byte of the diff line. */
  svn_stringbuf_appendbytes(buf, hunk_header[add], header_len);
  svn_stringbuf_appendbyte(buf, add ? '+' : '-');
  svn_stringbuf_appendbytes(buf, line, len);
  svn_stringbuf_appendbyte(buf, '\n');
  svn_stringbuf_appendcstr(buf, "\\ No newline at end of hunk\n");

  hunk->diff_text_range.end = buf->len;

  SVN_ERR(svn_io_open_unique_file3(&hunk->apr_file, NULL /* filename */,
                                   NULL /* system tempdir */,
                                   svn_io_file_del_on_pool_cleanup,
                                   result_pool, scratch_pool));
  SVN_ERR(svn_io_file_write_full(hunk->apr_file,
                                 buf->data, buf->len,
                                 NULL, scratch_pool));
  /* No need to seek. */

  *hunk_out = hunk;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff_hunk__create_adds_single_line(svn_diff_hunk_t **hunk_out,
                                       const char *line,
                                       const svn_patch_t *patch,
                                       apr_pool_t *result_pool,
                                       apr_pool_t *scratch_pool)
{
  SVN_ERR(add_or_delete_single_line(hunk_out, line, patch, 
                                    (!patch->reverse),
                                    result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff_hunk__create_deletes_single_line(svn_diff_hunk_t **hunk_out,
                                          const char *line,
                                          const svn_patch_t *patch,
                                          apr_pool_t *result_pool,
                                          apr_pool_t *scratch_pool)
{
  SVN_ERR(add_or_delete_single_line(hunk_out, line, patch,
                                    patch->reverse,
                                    result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

svn_linenum_t
svn_diff_hunk__get_fuzz_penalty(const svn_diff_hunk_t *hunk)
{
  return hunk->patch->reverse ? hunk->original_fuzz : hunk->modified_fuzz;
}

/* Baton for the base85 stream implementation */
struct base85_baton_t
{
  apr_file_t *file;
  apr_pool_t *iterpool;
  char buffer[52];        /* Bytes on current line */
  apr_off_t next_pos;     /* Start position of next line */
  apr_off_t end_pos;      /* Position after last line */
  apr_size_t buf_size;    /* Bytes available (52 unless at eof) */
  apr_size_t buf_pos;     /* Bytes in linebuffer */
  svn_boolean_t done;     /* At eof? */
};

/* Implements svn_read_fn_t for the base85 read stream */
static svn_error_t *
read_handler_base85(void *baton, char *buffer, apr_size_t *len)
{
  struct base85_baton_t *b85b = baton;
  apr_pool_t *iterpool = b85b->iterpool;
  apr_size_t remaining = *len;
  char *dest = buffer;

  svn_pool_clear(iterpool);

  if (b85b->done)
    {
      *len = 0;
      return SVN_NO_ERROR;
    }

  while (remaining && (b85b->buf_size > b85b->buf_pos
                       || b85b->next_pos < b85b->end_pos))
    {
      svn_stringbuf_t *line;
      svn_boolean_t at_eof;

      apr_size_t available = b85b->buf_size - b85b->buf_pos;
      if (available)
        {
          apr_size_t n = (remaining < available) ? remaining : available;

          memcpy(dest, b85b->buffer + b85b->buf_pos, n);
          dest += n;
          remaining -= n;
          b85b->buf_pos += n;

          if (!remaining)
            return SVN_NO_ERROR; /* *len = OK */
        }

      if (b85b->next_pos >= b85b->end_pos)
        break; /* At EOF */
      SVN_ERR(svn_io_file_seek(b85b->file, APR_SET, &b85b->next_pos,
                               iterpool));
      SVN_ERR(svn_io_file_readline(b85b->file, &line, NULL, &at_eof,
                                   APR_SIZE_MAX, iterpool, iterpool));
      if (at_eof)
        b85b->next_pos = b85b->end_pos;
      else
        {
          SVN_ERR(svn_io_file_get_offset(&b85b->next_pos, b85b->file,
                                         iterpool));
        }

      if (line->len && line->data[0] >= 'A' && line->data[0] <= 'Z')
        b85b->buf_size = line->data[0] - 'A' + 1;
      else if (line->len && line->data[0] >= 'a' && line->data[0] <= 'z')
        b85b->buf_size = line->data[0] - 'a' + 26 + 1;
      else
        return svn_error_create(SVN_ERR_DIFF_UNEXPECTED_DATA, NULL,
                                _("Unexpected data in base85 section"));

      if (b85b->buf_size < 52)
        b85b->next_pos = b85b->end_pos; /* Handle as EOF */

      SVN_ERR(svn_diff__base85_decode_line(b85b->buffer, b85b->buf_size,
                                           line->data + 1, line->len - 1,
                                           iterpool));
      b85b->buf_pos = 0;
    }

  *len -= remaining;
  b85b->done = TRUE;

  return SVN_NO_ERROR;
}

/* Implements svn_close_fn_t for the base85 read stream */
static svn_error_t *
close_handler_base85(void *baton)
{
  struct base85_baton_t *b85b = baton;

  svn_pool_destroy(b85b->iterpool);

  return SVN_NO_ERROR;
}

/* Gets a stream that reads decoded base85 data from a segment of a file.
   The current implementation might assume that both start_pos and end_pos
   are located at line boundaries. */
static svn_stream_t *
get_base85_data_stream(apr_file_t *file,
                       apr_off_t start_pos,
                       apr_off_t end_pos,
                       apr_pool_t *result_pool)
{
  struct base85_baton_t *b85b = apr_pcalloc(result_pool, sizeof(*b85b));
  svn_stream_t *base85s = svn_stream_create(b85b, result_pool);

  b85b->file = file;
  b85b->iterpool = svn_pool_create(result_pool);
  b85b->next_pos = start_pos;
  b85b->end_pos = end_pos;

  svn_stream_set_read2(base85s, NULL /* only full read support */,
                       read_handler_base85);
  svn_stream_set_close(base85s, close_handler_base85);
  return base85s;
}

/* Baton for the length verification stream functions */
struct length_verify_baton_t
{
  svn_stream_t *inner;
  svn_filesize_t remaining;
};

/* Implements svn_read_fn_t for the length verification stream */
static svn_error_t *
read_handler_length_verify(void *baton, char *buffer, apr_size_t *len)
{
  struct length_verify_baton_t *lvb = baton;
  apr_size_t requested_len = *len;

  SVN_ERR(svn_stream_read_full(lvb->inner, buffer, len));

  if (*len > lvb->remaining)
    return svn_error_create(SVN_ERR_DIFF_UNEXPECTED_DATA, NULL,
                            _("Base85 data expands to longer than declared "
                              "filesize"));
  else if (requested_len > *len && *len != lvb->remaining)
    return svn_error_create(SVN_ERR_DIFF_UNEXPECTED_DATA, NULL,
                            _("Base85 data expands to smaller than declared "
                              "filesize"));

  lvb->remaining -= *len;

  return SVN_NO_ERROR;
}

/* Implements svn_close_fn_t for the length verification stream */
static svn_error_t *
close_handler_length_verify(void *baton)
{
  struct length_verify_baton_t *lvb = baton;

  return svn_error_trace(svn_stream_close(lvb->inner));
}

/* Gets a stream that verifies on reads that the inner stream is exactly
   of the specified length */
static svn_stream_t *
get_verify_length_stream(svn_stream_t *inner,
                         svn_filesize_t expected_size,
                         apr_pool_t *result_pool)
{
  struct length_verify_baton_t *lvb = apr_palloc(result_pool, sizeof(*lvb));
  svn_stream_t *len_stream = svn_stream_create(lvb, result_pool);

  lvb->inner = inner;
  lvb->remaining = expected_size;

  svn_stream_set_read2(len_stream, NULL /* only full read support */,
                       read_handler_length_verify);
  svn_stream_set_close(len_stream, close_handler_length_verify);

  return len_stream;
}

svn_stream_t *
svn_diff_get_binary_diff_original_stream(const svn_diff_binary_patch_t *bpatch,
                                         apr_pool_t *result_pool)
{
  svn_stream_t *s = get_base85_data_stream(bpatch->apr_file, bpatch->src_start,
                                           bpatch->src_end, result_pool);

  s = svn_stream_compressed(s, result_pool);

  /* ### If we (ever) want to support the DELTA format, then we should hook the
         undelta handling here */

  return get_verify_length_stream(s, bpatch->src_filesize, result_pool);
}

svn_stream_t *
svn_diff_get_binary_diff_result_stream(const svn_diff_binary_patch_t *bpatch,
                                       apr_pool_t *result_pool)
{
  svn_stream_t *s = get_base85_data_stream(bpatch->apr_file, bpatch->dst_start,
                                           bpatch->dst_end, result_pool);

  s = svn_stream_compressed(s, result_pool);

  /* ### If we (ever) want to support the DELTA format, then we should hook the
  undelta handling here */

  return get_verify_length_stream(s, bpatch->dst_filesize, result_pool);
}

 * is being read. NO_FINAL_EOL declares if the hunk contains a no final
 * EOL marker.
                                   svn_boolean_t no_final_eol,
  const char *eol_p;
  apr_pool_t *last_pool;

  if (!eol)
    eol = &eol_p;
      *eol = NULL;
      *stringbuf = svn_stringbuf_create_empty(result_pool);
  SVN_ERR(svn_io_file_get_offset(&pos, file, scratch_pool));

  /* It's not ITERPOOL because we use data allocated in LAST_POOL out
     of the loop. */
  last_pool = svn_pool_create(scratch_pool);
      svn_pool_clear(last_pool);

      SVN_ERR(svn_io_file_readline(file, &str, eol, eof, max_len,
                                   last_pool, last_pool));
      SVN_ERR(svn_io_file_get_offset(&range->current, file, last_pool));
      *eol = NULL;
      /* Return the line as-is. Handle as a chopped leading spaces */
  if (!filtered && *eof && !*eol && *str->data)
    {
      /* Ok, we miss a final EOL in the patch file, but didn't see a
         no eol marker line.

         We should report that we had an EOL or the patch code will
         misbehave (and it knows nothing about no eol markers) */

      if (!no_final_eol && eol != &eol_p)
        {
          apr_off_t start = 0;

          SVN_ERR(svn_io_file_seek(file, APR_SET, &start, scratch_pool));

          SVN_ERR(svn_io_file_readline(file, &str, eol, NULL, APR_SIZE_MAX,
                                       scratch_pool, scratch_pool));

          /* Every patch file that has hunks has at least one EOL*/
          SVN_ERR_ASSERT(*eol != NULL);
        }

      *eof = FALSE;
      /* Fall through to seek back to the right location */
    }
  svn_pool_destroy(last_pool);
                                       hunk->patch->reverse
                                          ? hunk->modified_no_final_eol
                                          : hunk->original_no_final_eol,
                                       hunk->patch->reverse
                                          ? hunk->original_no_final_eol
                                          : hunk->modified_no_final_eol,
  const char *eol_p;

  if (!eol)
    eol = &eol_p;
      *eol = NULL;
      *stringbuf = svn_stringbuf_create_empty(result_pool);
  SVN_ERR(svn_io_file_get_offset(&pos, hunk->apr_file, scratch_pool));
  SVN_ERR(svn_io_file_readline(hunk->apr_file, &line, eol, eof, max_len,
                               result_pool,
  SVN_ERR(svn_io_file_get_offset(&hunk->diff_text_range.current,
                                 hunk->apr_file, scratch_pool));
  if (*eof && !*eol && *line->data)
      /* Ok, we miss a final EOL in the patch file, but didn't see a
          no eol marker line.

          We should report that we had an EOL or the patch code will
          misbehave (and it knows nothing about no eol markers) */

      if (eol != &eol_p)
          /* Lets pick the first eol we find in our patch file */
          apr_off_t start = 0;
          svn_stringbuf_t *str;

          SVN_ERR(svn_io_file_seek(hunk->apr_file, APR_SET, &start,
                                   scratch_pool));

          SVN_ERR(svn_io_file_readline(hunk->apr_file, &str, eol, NULL,
                                       APR_SIZE_MAX,
                                       scratch_pool, scratch_pool));

          /* Every patch file that has hunks has at least one EOL*/
          SVN_ERR_ASSERT(*eol != NULL);

      *eof = FALSE;

      /* Fall through to seek back to the right location */
    }

  SVN_ERR(svn_io_file_seek(hunk->apr_file, APR_SET, &pos, scratch_pool));

  if (hunk->patch->reverse)
    {
      if (line->data[0] == '+')
        line->data[0] = '-';
      else if (line->data[0] == '-')
        line->data[0] = '+';

/* A helper function to parse svn:mergeinfo diffs.
 *
 * These diffs use a special pretty-print format, for instance:
 *
 * Added: svn:mergeinfo
 * ## -0,0 +0,1 ##
 *   Merged /trunk:r2-3
 *
 * The hunk header has the following format:
 * ## -0,NUMBER_OF_REVERSE_MERGES +0,NUMBER_OF_FORWARD_MERGES ##
 *
 * At this point, the number of reverse merges has already been
 * parsed into HUNK->ORIGINAL_LENGTH, and the number of forward
 * merges has been parsed into HUNK->MODIFIED_LENGTH.
 *
 * The header is followed by a list of mergeinfo, one path per line.
 * This function parses such lines. Lines describing reverse merges
 * appear first, and then all lines describing forward merges appear.
 *
 * Parts of the line are affected by i18n. The words 'Merged'
 * and 'Reverse-merged' can appear in any language and at any
 * position within the line. We can only assume that a leading
 * '/' starts the merge source path, the path is followed by
 * ":r", which in turn is followed by a mergeinfo revision range,
 *  which is terminated by whitespace or end-of-string.
 *
 * If the current line meets the above criteria and we're able
 * to parse valid mergeinfo from it, the resulting mergeinfo
 * is added to patch->mergeinfo or patch->reverse_mergeinfo,
 * and we proceed to the next line.
 */
static svn_error_t *
parse_mergeinfo(svn_boolean_t *found_mergeinfo,
                svn_stringbuf_t *line,
                svn_diff_hunk_t *hunk,
                svn_patch_t *patch,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  char *slash = strchr(line->data, '/');
  char *colon = strrchr(line->data, ':');

  *found_mergeinfo = FALSE;

  if (slash && colon && colon[1] == 'r' && slash < colon)
    {
      svn_stringbuf_t *input;
      svn_mergeinfo_t mergeinfo = NULL;
      char *s;
      svn_error_t *err;

      input = svn_stringbuf_create_ensure(line->len, scratch_pool);

      /* Copy the merge source path + colon */
      s = slash;
      while (s <= colon)
        {
          svn_stringbuf_appendbyte(input, *s);
          s++;
        }

      /* skip 'r' after colon */
      s++;

      /* Copy the revision range. */
      while (s < line->data + line->len)
        {
          if (svn_ctype_isspace(*s))
            break;
          svn_stringbuf_appendbyte(input, *s);
          s++;
        }

      err = svn_mergeinfo_parse(&mergeinfo, input->data, result_pool);
      if (err && err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
        {
          svn_error_clear(err);
          mergeinfo = NULL;
        }
      else
        SVN_ERR(err);

      if (mergeinfo)
        {
          if (hunk->original_length > 0) /* reverse merges */
            {
              if (patch->reverse)
                {
                  if (patch->mergeinfo == NULL)
                    patch->mergeinfo = mergeinfo;
                  else
                    SVN_ERR(svn_mergeinfo_merge2(patch->mergeinfo,
                                                 mergeinfo,
                                                 result_pool,
                                                 scratch_pool));
                }
              else
                {
                  if (patch->reverse_mergeinfo == NULL)
                    patch->reverse_mergeinfo = mergeinfo;
                  else
                    SVN_ERR(svn_mergeinfo_merge2(patch->reverse_mergeinfo,
                                                 mergeinfo,
                                                 result_pool,
                                                 scratch_pool));
                }
              hunk->original_length--;
            }
          else if (hunk->modified_length > 0) /* forward merges */
            {
              if (patch->reverse)
                {
                  if (patch->reverse_mergeinfo == NULL)
                    patch->reverse_mergeinfo = mergeinfo;
                  else
                    SVN_ERR(svn_mergeinfo_merge2(patch->reverse_mergeinfo,
                                                 mergeinfo,
                                                 result_pool,
                                                 scratch_pool));
                }
              else
                {
                  if (patch->mergeinfo == NULL)
                    patch->mergeinfo = mergeinfo;
                  else
                    SVN_ERR(svn_mergeinfo_merge2(patch->mergeinfo,
                                                 mergeinfo,
                                                 result_pool,
                                                 scratch_pool));
                }
              hunk->modified_length--;
            }

          *found_mergeinfo = TRUE;
        }
    }

  return SVN_NO_ERROR;
}

  svn_boolean_t original_no_final_eol = FALSE;
  svn_boolean_t modified_no_final_eol = FALSE;
  /* Get current seek position. */
  SVN_ERR(svn_io_file_get_offset(&pos, apr_file, scratch_pool));
      SVN_ERR(svn_io_file_readline(apr_file, &line, NULL, &eof, APR_SIZE_MAX,
                                   iterpool, iterpool));
      SVN_ERR(svn_io_file_get_offset(&pos, apr_file, iterpool));
      /* Lines starting with a backslash indicate a missing EOL:
       * "\ No newline at end of file" or "end of property". */
          if (in_hunk)
              /* Set for the type and context by using != the other type */
              if (last_line_type != modified_line)
                original_no_final_eol = TRUE;
              if (last_line_type != original_line)
                modified_no_final_eol = TRUE;
      if (in_hunk && *is_property && *prop_name &&
          strcmp(*prop_name, SVN_PROP_MERGEINFO) == 0)
        {
          svn_boolean_t found_mergeinfo;

          SVN_ERR(parse_mergeinfo(&found_mergeinfo, line, *hunk, patch,
                                  result_pool, iterpool));
          if (found_mergeinfo)
            continue; /* Proceed to the next line in the svn:mergeinfo hunk. */
          else
            {
              /* Perhaps we can also use original_lines/modified_lines here */

              in_hunk = FALSE; /* On to next property */
            }
        }

          if (c == ' '
              || ((original_lines > 0 && modified_lines > 0)
                  && ( 
                      (! eof && line->len == 0)
                      || (ignore_whitespace && c != del && c != add))))
              if (original_lines > 0)
                original_lines--;
              else
                {
                  (*hunk)->original_length++;
                  (*hunk)->original_fuzz++;
                }
              if (modified_lines > 0)
                modified_lines--;
              else
                {
                  (*hunk)->modified_length++;
                  (*hunk)->modified_fuzz++;
                }
          else if (c == del
                   && (original_lines > 0 || line->data[1] != del))
              if (original_lines > 0)
                original_lines--;
              else
                {
                  (*hunk)->original_length++;
                  (*hunk)->original_fuzz++;
                }
          else if (c == add
                   && (modified_lines > 0 || line->data[1] != add))
              if (modified_lines > 0)
                modified_lines--;
              else
                {
                  (*hunk)->modified_length++;
                  (*hunk)->modified_fuzz++;
                }
                *prop_operation = (patch->reverse ? svn_diff_op_deleted
                                                  : svn_diff_op_added);
                *prop_operation = (patch->reverse ? svn_diff_op_added
                                                  : svn_diff_op_deleted);
      /* Did we get the number of context lines announced in the header?

         If not... let's limit the number from the header to what we
         actually have, and apply a fuzz penalty */
      if (original_lines)
        {
          (*hunk)->original_length -= original_lines;
          (*hunk)->original_fuzz += original_lines;
        }
      if (modified_lines)
        {
          (*hunk)->modified_length -= modified_lines;
          (*hunk)->modified_fuzz += modified_lines;
        }

      (*hunk)->original_no_final_eol = original_no_final_eol;
      (*hunk)->modified_no_final_eol = modified_no_final_eol;
   state_start,             /* initial */
   state_git_diff_seen,     /* diff --git */
   state_git_tree_seen,     /* a tree operation, rather than content change */
   state_git_minus_seen,    /* --- /dev/null; or --- a/ */
   state_git_plus_seen,     /* +++ /dev/null; or +++ a/ */
   state_old_mode_seen,     /* old mode 100644 */
   state_git_mode_seen,     /* new mode 100644 */
   state_move_from_seen,    /* rename from foo.c */
   state_copy_from_seen,    /* copy from foo.c */
   state_minus_seen,        /* --- foo.c */
   state_unidiff_found,     /* valid start of a regular unidiff header */
   state_git_header_found,  /* valid start of a --git diff header */
   state_binary_patch_found /* valid start of binary patch */
      ptrdiff_t len_old;
      ptrdiff_t len_new;
/* Helper for git_old_mode() and git_new_mode().  Translate the git
 * file mode MODE_STR into a binary "executable?" and "symlink?" state. */
static svn_error_t *
parse_git_mode_bits(svn_tristate_t *executable_p,
                    svn_tristate_t *symlink_p,
                    const char *mode_str)
{
  apr_uint64_t mode;
  SVN_ERR(svn_cstring_strtoui64(&mode, mode_str,
                                0 /* min */,
                                0777777 /* max: six octal digits */,
                                010 /* radix (octal) */));

  /* Note: 0644 and 0755 are the only modes that can occur for plain files.
   * We deliberately choose to parse only those values: we are strict in what
   * we accept _and_ in what we produce.
   *
   * (Having said that, though, we could consider relaxing the parser to also
   * map
   *     (mode & 0111) == 0000 -> svn_tristate_false
   *     (mode & 0111) == 0111 -> svn_tristate_true
   *        [anything else]    -> svn_tristate_unknown
   * .)
   */

  switch (mode & 0777)
    {
      case 0644:
        *executable_p = svn_tristate_false;
        break;

      case 0755:
        *executable_p = svn_tristate_true;
        break;

      default:
        /* Ignore unknown values. */
        *executable_p = svn_tristate_unknown;
        break;
    }

  switch (mode & 0170000 /* S_IFMT */)
    {
      case 0120000: /* S_IFLNK */
        *symlink_p = svn_tristate_true;
        break;

      case 0100000: /* S_IFREG */
      case 0040000: /* S_IFDIR */
        *symlink_p = svn_tristate_false;
        break;

      default:
        /* Ignore unknown values.
           (Including those generated by Subversion <= 1.9) */
        *symlink_p = svn_tristate_unknown;
        break;
    }

  return SVN_NO_ERROR;
}

/* Parse the 'old mode ' line of a git extended unidiff. */
static svn_error_t *
git_old_mode(enum parse_state *new_state, char *line, svn_patch_t *patch,
             apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  SVN_ERR(parse_git_mode_bits(&patch->old_executable_bit,
                              &patch->old_symlink_bit,
                              line + STRLEN_LITERAL("old mode ")));

#ifdef SVN_DEBUG
  /* If this assert trips, the "old mode" is neither ...644 nor ...755 . */
  SVN_ERR_ASSERT(patch->old_executable_bit != svn_tristate_unknown);
#endif

  *new_state = state_old_mode_seen;
  return SVN_NO_ERROR;
}

/* Parse the 'new mode ' line of a git extended unidiff. */
static svn_error_t *
git_new_mode(enum parse_state *new_state, char *line, svn_patch_t *patch,
             apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  SVN_ERR(parse_git_mode_bits(&patch->new_executable_bit,
                              &patch->new_symlink_bit,
                              line + STRLEN_LITERAL("new mode ")));

#ifdef SVN_DEBUG
  /* If this assert trips, the "old mode" is neither ...644 nor ...755 . */
  SVN_ERR_ASSERT(patch->new_executable_bit != svn_tristate_unknown);
#endif

  /* Don't touch patch->operation. */

  *new_state = state_git_mode_seen;
  return SVN_NO_ERROR;
}

static svn_error_t *
git_index(enum parse_state *new_state, char *line, svn_patch_t *patch,
          apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  /* We either have something like "index 33e5b38..0000000" (which we just
     ignore as we are not interested in git specific shas) or something like
     "index 33e5b38..0000000 120000" which tells us the mode, that isn't
     changed by applying this patch.

     If the mode would have changed then we would see 'old mode' and 'new mode'
     lines.
  */
  line = strchr(line + STRLEN_LITERAL("index "), ' ');

  if (line && patch->new_executable_bit == svn_tristate_unknown
           && patch->new_symlink_bit == svn_tristate_unknown
           && patch->operation != svn_diff_op_added
           && patch->operation != svn_diff_op_deleted)
    {
      SVN_ERR(parse_git_mode_bits(&patch->new_executable_bit,
                                  &patch->new_symlink_bit,
                                  line + 1));

      /* There is no change.. so set the old values to the new values */
      patch->old_executable_bit = patch->new_executable_bit;
      patch->old_symlink_bit = patch->new_symlink_bit;
    }

  /* This function doesn't change the state! */
  /* *new_state = *new_state */
  return SVN_NO_ERROR;
}

  SVN_ERR(parse_git_mode_bits(&patch->new_executable_bit,
                              &patch->new_symlink_bit,
                              line + STRLEN_LITERAL("new file mode ")));

  SVN_ERR(parse_git_mode_bits(&patch->old_executable_bit,
                              &patch->old_symlink_bit,
                              line + STRLEN_LITERAL("deleted file mode ")));

/* Parse the 'GIT binary patch' header */
static svn_error_t *
binary_patch_start(enum parse_state *new_state, char *line, svn_patch_t *patch,
             apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  *new_state = state_binary_patch_found;
  return SVN_NO_ERROR;
}


  prop_patch = svn_hash_gets(patch->prop_patches, prop_name);
      svn_hash_sets(patch->prop_patches, prop_name, prop_patch);
                           APR_READ | APR_BUFFERED, APR_OS_DEFAULT,
                           result_pool));

          /* Skip svn:mergeinfo properties.
           * Mergeinfo data cannot be represented as a hunk and
           * is therefore stored in PATCH itself. */
          if (strcmp(prop_name, SVN_PROP_MERGEINFO) == 0)
            continue;

static svn_error_t *
parse_binary_patch(svn_patch_t *patch, apr_file_t *apr_file,
                   svn_boolean_t reverse,
                   apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_off_t pos, last_line;
  svn_stringbuf_t *line;
  svn_boolean_t eof = FALSE;
  svn_diff_binary_patch_t *bpatch = apr_pcalloc(result_pool, sizeof(*bpatch));
  svn_boolean_t in_blob = FALSE;
  svn_boolean_t in_src = FALSE;

  bpatch->apr_file = apr_file;

  patch->prop_patches = apr_hash_make(result_pool);

  SVN_ERR(svn_io_file_get_offset(&pos, apr_file, scratch_pool));

  while (!eof)
    {
      last_line = pos;
      SVN_ERR(svn_io_file_readline(apr_file, &line, NULL, &eof, APR_SIZE_MAX,
                               iterpool, iterpool));

      /* Update line offset for next iteration. */
      SVN_ERR(svn_io_file_get_offset(&pos, apr_file, iterpool));

      if (in_blob)
        {
          char c = line->data[0];

          /* 66 = len byte + (52/4*5) chars */
          if (((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
              && line->len <= 66
              && !strchr(line->data, ':')
              && !strchr(line->data, ' '))
            {
              /* One more blop line */
              if (in_src)
                bpatch->src_end = pos;
              else
                bpatch->dst_end = pos;
            }
          else if (svn_stringbuf_first_non_whitespace(line) < line->len
                   && !(in_src && bpatch->src_start < last_line))
            {
              break; /* Bad patch */
            }
          else if (in_src)
            {
              patch->binary_patch = bpatch; /* SUCCESS! */
              break; 
            }
          else
            {
              in_blob = FALSE;
              in_src = TRUE;
            }
        }
      else if (starts_with(line->data, "literal "))
        {
          apr_uint64_t expanded_size;
          svn_error_t *err = svn_cstring_strtoui64(&expanded_size,
                                                   &line->data[8],
                                                   0, APR_UINT64_MAX, 10);

          if (err)
            {
              svn_error_clear(err);
              break;
            }

          if (in_src)
            {
              bpatch->src_start = pos;
              bpatch->src_filesize = expanded_size;
            }
          else
            {
              bpatch->dst_start = pos;
              bpatch->dst_filesize = expanded_size;
            }
          in_blob = TRUE;
        }
      else
        break; /* We don't support GIT deltas (yet) */
    }
  svn_pool_destroy(iterpool);

  if (!eof)
    /* Rewind to the start of the line just read, so subsequent calls
     * don't end up skipping the line. It may contain a patch or hunk header.*/
    SVN_ERR(svn_io_file_seek(apr_file, APR_SET, &last_line, scratch_pool));
  else if (in_src
           && ((bpatch->src_end > bpatch->src_start) || !bpatch->src_filesize))
    {
      patch->binary_patch = bpatch; /* SUCCESS */
    }

  /* Reverse patch if requested */
  if (reverse && patch->binary_patch)
    {
      apr_off_t tmp_start = bpatch->src_start;
      apr_off_t tmp_end = bpatch->src_end;
      svn_filesize_t tmp_filesize = bpatch->src_filesize;

      bpatch->src_start = bpatch->dst_start;
      bpatch->src_end = bpatch->dst_end;
      bpatch->src_filesize = bpatch->dst_filesize;

      bpatch->dst_start = tmp_start;
      bpatch->dst_end = tmp_end;
      bpatch->dst_filesize = tmp_filesize;
    }

  return SVN_NO_ERROR;
}

  {"--- ",              state_start,            diff_minus},
  {"+++ ",              state_minus_seen,       diff_plus},

  {"diff --git",        state_start,            git_start},
  {"--- a/",            state_git_diff_seen,    git_minus},
  {"--- a/",            state_git_mode_seen,    git_minus},
  {"--- a/",            state_git_tree_seen,    git_minus},
  {"--- /dev/null",     state_git_mode_seen,    git_minus},
  {"--- /dev/null",     state_git_tree_seen,    git_minus},
  {"+++ b/",            state_git_minus_seen,   git_plus},
  {"+++ /dev/null",     state_git_minus_seen,   git_plus},

  {"old mode ",         state_git_diff_seen,    git_old_mode},
  {"new mode ",         state_old_mode_seen,    git_new_mode},

  {"rename from ",      state_git_diff_seen,    git_move_from},
  {"rename from ",      state_git_mode_seen,    git_move_from},
  {"rename to ",        state_move_from_seen,   git_move_to},

  {"copy from ",        state_git_diff_seen,    git_copy_from},
  {"copy from ",        state_git_mode_seen,    git_copy_from},
  {"copy to ",          state_copy_from_seen,   git_copy_to},

  {"new file ",         state_git_diff_seen,    git_new_file},

  {"deleted file ",     state_git_diff_seen,    git_deleted_file},

  {"index ",            state_git_diff_seen,    git_index},
  {"index ",            state_git_tree_seen,    git_index},
  {"index ",            state_git_mode_seen,    git_index},

  {"GIT binary patch",  state_git_diff_seen,    binary_patch_start},
  {"GIT binary patch",  state_git_tree_seen,    binary_patch_start},
  {"GIT binary patch",  state_git_mode_seen,    binary_patch_start},
svn_diff_parse_next_patch(svn_patch_t **patch_p,
  svn_patch_t *patch;
      *patch_p = NULL;
  patch = apr_pcalloc(result_pool, sizeof(*patch));
  patch->old_executable_bit = svn_tristate_unknown;
  patch->new_executable_bit = svn_tristate_unknown;
  patch->old_symlink_bit = svn_tristate_unknown;
  patch->new_symlink_bit = svn_tristate_unknown;
      SVN_ERR(svn_io_file_readline(patch_file->apr_file, &line, NULL, &eof,
                                   APR_SIZE_MAX, iterpool, iterpool));
          SVN_ERR(svn_io_file_get_offset(&pos, patch_file->apr_file,
                                         iterpool));
              SVN_ERR(transitions[i].fn(&state, line->data, patch,
      if (state == state_unidiff_found
          || state == state_git_header_found
          || state == state_binary_patch_found)
      else if ((state == state_git_tree_seen || state == state_git_mode_seen)
               && line_after_tree_header_read
               && !valid_header_line)
          /* We have a valid diff header for a patch with only tree changes.
           * Rewind to the start of the line just read, so subsequent calls
           * to this function don't end up skipping the line -- it may
           * contain a patch. */
          SVN_ERR(svn_io_file_seek(patch_file->apr_file, APR_SET, &last_line,
                                   scratch_pool));
          break;
      else if (state == state_git_tree_seen
               || state == state_git_mode_seen)
               && state != state_git_diff_seen)
  patch->reverse = reverse;
      svn_tristate_t ts_tmp;

      temp = patch->old_filename;
      patch->old_filename = patch->new_filename;
      patch->new_filename = temp;

      switch (patch->operation)
        {
          case svn_diff_op_added:
            patch->operation = svn_diff_op_deleted;
            break;
          case svn_diff_op_deleted:
            patch->operation = svn_diff_op_added;
            break;

          case svn_diff_op_modified:
            break; /* Stays modified. */

          case svn_diff_op_copied:
          case svn_diff_op_moved:
            break; /* Stays copied or moved, just in the other direction. */
          case svn_diff_op_unchanged:
            break; /* Stays unchanged, of course. */
        }

      ts_tmp = patch->old_executable_bit;
      patch->old_executable_bit = patch->new_executable_bit;
      patch->new_executable_bit = ts_tmp;

      ts_tmp = patch->old_symlink_bit;
      patch->old_symlink_bit = patch->new_symlink_bit;
      patch->new_symlink_bit = ts_tmp;
  if (patch->old_filename == NULL || patch->new_filename == NULL)
      patch = NULL;
    {
      if (state == state_binary_patch_found)
        {
          SVN_ERR(parse_binary_patch(patch, patch_file->apr_file, reverse,
                                     result_pool, iterpool));
          /* And fall through in property parsing */
        }

      SVN_ERR(parse_hunks(patch, patch_file->apr_file, ignore_whitespace,
                          result_pool, iterpool));
    }
  SVN_ERR(svn_io_file_get_offset(&patch_file->next_patch_offset,
                                 patch_file->apr_file, scratch_pool));
  if (patch && patch->hunks)
      svn_sort__array(patch->hunks, compare_hunks);
  *patch_p = patch;