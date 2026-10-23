import http.server, os, re, socketserver
DIR=os.path.dirname(os.path.abspath(__file__))
class H(http.server.SimpleHTTPRequestHandler):
    def __init__(self,*a,**k): super().__init__(*a,directory=DIR,**k)
    def do_GET(self):
        path=self.translate_path(self.path)
        if not os.path.isfile(path): self.send_error(404); return
        size=os.path.getsize(path); rng=self.headers.get("Range")
        f=open(path,"rb")
        if rng:
            m=re.match(r"bytes=(\d+)-(\d*)",rng); start=int(m.group(1)); end=int(m.group(2)) if m.group(2) else size-1
            end=min(end,size-1); length=end-start+1
            self.send_response(206)
            self.send_header("Content-Type","application/octet-stream")
            self.send_header("Content-Range",f"bytes {start}-{end}/{size}")
            self.send_header("Accept-Ranges","bytes")
            self.send_header("Content-Length",str(length)); self.end_headers()
            f.seek(start); remaining=length
            try:
                while remaining>0:
                    buf=f.read(min(65536,remaining));
                    if not buf: break
                    self.wfile.write(buf); remaining-=len(buf)
            except (BrokenPipeError,ConnectionResetError): pass
            finally: f.close()
        else:
            self.send_response(200)
            self.send_header("Content-Type","application/octet-stream")
            self.send_header("Accept-Ranges","bytes")
            self.send_header("Content-Length",str(size)); self.end_headers()
            try: self.copyfile(f,self.wfile)
            except (BrokenPipeError,ConnectionResetError): pass
            finally: f.close()
    def do_HEAD(self):
        path=self.translate_path(self.path)
        if not os.path.isfile(path): self.send_error(404); return
        self.send_response(200); self.send_header("Content-Type","application/octet-stream")
        self.send_header("Accept-Ranges","bytes"); self.send_header("Content-Length",str(os.path.getsize(path))); self.end_headers()
class TS(socketserver.ThreadingTCPServer): allow_reuse_address=True; daemon_threads=True
TS(("0.0.0.0",8099),H).serve_forever()
