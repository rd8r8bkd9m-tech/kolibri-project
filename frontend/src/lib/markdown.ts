import MarkdownIt from "markdown-it";
import DOMPurify from "dompurify";

const md = new MarkdownIt({
  html: false,
  linkify: true,
  typographer: true,
  breaks: true
});

export function renderMarkdown(raw: string) {
  const html = md.render(raw);
  return DOMPurify.sanitize(html);
}
