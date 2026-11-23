To use, run 
socat -d -d \
  PTY,link=/tmp/fs,raw,echo=0 \
  PTY,link=/tmp/fm,raw,echo=0