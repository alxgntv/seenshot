export function sharePage(opts: {
  publicId: string;
  imageUrl: string;
  missing?: boolean;
  abuseEmail: string;
}): string {
  if (opts.missing) {
    return `<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="robots" content="noindex">
<title>SeenShot</title></head>
<body style="font-family:sans-serif;padding:40px">
<h1>This screenshot is gone</h1>
<p>The link is expired, unpublished, or was removed.</p>
</body></html>`;
  }
  return `<!doctype html><html lang="en"><head>
<meta charset="utf-8">
<meta name="robots" content="noindex,nofollow">
<meta property="og:image" content="${opts.imageUrl}">
<title>SeenShot</title>
</head>
<body style="margin:0;background:#111;color:#fff;font-family:sans-serif;text-align:center">
<img src="${opts.imageUrl}" alt="Screenshot" style="max-width:100%;height:auto">
<p><a href="/v1/abuse?id=${opts.publicId}" style="color:#9cf">Report</a> · ${opts.abuseEmail}</p>
</body></html>`;
}

export function tosPage(abuseEmail: string): string {
  return `<!doctype html><html lang="en"><head><meta charset="utf-8"><title>SeenShot Terms</title></head>
<body style="font-family:sans-serif;max-width:720px;margin:40px auto">
<h1>SeenShot Terms</h1>
<p>Do not upload illegal content. Report abuse to ${abuseEmail}.</p>
<p>We may remove public screenshots after a report.</p>
</body></html>`;
}
