# encoding: utf-8
# markdown_mini.rb — the hermetic markdown subset renderer (zero dependencies).
#
# Supports exactly what the translations use:
#   ## ### headings, **bold**, *italic*, `code`, [text](url),
#   - / * lists, > blockquote, --- rule, paragraphs,
#   HTML entities preserved (&nbsp; &ldquo; &ge; …), raw < > & escaped.
module MarkdownMini
  ENTITY = /&(?:\#\d+|\#x[0-9a-fA-F]+|[a-zA-Z][a-zA-Z0-9]{1,10});/

  def self.escape(src)
    stash = {}
    i = 0
    s = src.to_s.gsub(ENTITY) { |m| i += 1; stash[i] = m; "\x00#{i}\x01" }
    s = s.gsub('&', '&amp;').gsub('<', '&lt;').gsub('>', '&gt;')
    s.gsub(/\x00(\d+)\x01/) { stash[Regexp.last_match(1).to_i] }
  end

  def self.inline(src)
    s = escape(src)
    # code spans first (their contents stay literal)
    s = s.gsub(/`([^`\n]+)`/) { "<code>#{Regexp.last_match(1)}</code>" }
    # links
    s = s.gsub(/\[([^\]]+)\]\(([^)\s]+)\)/) { "<a href=\"#{Regexp.last_match(2)}\">#{Regexp.last_match(1)}</a>" }
    # bold then italic
    s = s.gsub(/\*\*([^*\n]+)\*\*/) { "<strong>#{Regexp.last_match(1)}</strong>" }
    s = s.gsub(/(^|[^*])\*([^*\n]+)\*(?!\*)/) { "#{Regexp.last_match(1)}<em>#{Regexp.last_match(2)}</em>" }
    s
  end

  def self.render(src)
    out = []
    list = nil      # :ul or :ol, nil = closed
    quote = false
    para = []
    flush_para = lambda do
      unless para.empty?
        out << "<p>#{inline(para.join(' '))}</p>"
        para.clear
      end
    end
    close_list = lambda do
      if list
        out << "</#{list}>"
        list = nil
      end
    end
    src.to_s.gsub("\r\n", "\n").split("\n").each do |line|
      s = line.rstrip
      if s.strip.empty?
        flush_para.call; close_list.call
        if quote; out << '</blockquote>'; quote = false; end
        next
      end
      if s =~ /\A(#+)\s+(.+)/
        flush_para.call; close_list.call
        level = [Regexp.last_match(1).length + 1, 6].min
        out << "<h#{level}>#{inline(Regexp.last_match(2))}</h#{level}>"
        next
      end
      if s.strip == '---'
        flush_para.call; close_list.call
        out << '<hr/>'
        next
      end
      if s =~ /\A>\s?(.*)/
        flush_para.call; close_list.call
        out << '<blockquote>' unless quote
        quote = true
        out << "<p>#{inline(Regexp.last_match(1))}</p>"
        next
      end
      if s =~ /\A[-*]\s+(.+)/
        flush_para.call
        out << '<ul>' unless list == :ul
        list = :ul
        out << "<li>#{inline(Regexp.last_match(1))}</li>"
        next
      end
      if s =~ /\A\d+\.\s+(.+)/
        flush_para.call
        out << '<ol>' unless list == :ol
        list = :ol
        out << "<li>#{inline(Regexp.last_match(1))}</li>"
        next
      end
      para << s
    end
    flush_para.call; close_list.call
    out << '</blockquote>' if quote
    out.join("\n")
  end
end
