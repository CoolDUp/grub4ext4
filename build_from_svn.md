# Build grub4ext4 from svn source code #

Typically, one can use quilt(http://ftp.twaren.net/Unix/NonGNU/quilt/) to manage these patches

1. checkout the source code:

  * svn checkout http://grub4ext4.googlecode.com/svn/trunk/ grub4ext4-read-only

2. setup the source tree:

  * quilt setup grub.spec

  * cd grub-0.97

  * quilt push -a

3. build grub

  * configure

  * make