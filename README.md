
# SizzlingPlugins
- - -

Various client and server plugins developed in C++ for Team Fortress(R) 2 and the Source(R) Engine.


## Building
- - -

In the parent directory of `SizzlingPlugins.sln` file is in, checkout hl2sdk-ob-valve, and switch to the `sizzlingplugins` branch.

To have the plugins automatically installed, create a `TF2_DIR.txt` file in the same directory as `SizzlingPlugins.sln`, with a path to your teamfortress directory (ex. `D:\Program Files\Steam\steamapps\jordan427\Team Fortress 2\tf`).

In order to actually run the client side plugins (sizzlingrecord), you need to use the `-insecure` flag in the launch options for TF2. ([Source Documentation](https://developer.valvesoftware.com/wiki/Client_plugins))

### Windows
 
Use the provided solution, and build.

## Contact
- - -

IRC: [irc.gamesurge.net / #sizzlingchat](irc://irc.gamesurge.net/sizzlingchat)


## License 
- - -

Copyright (c) 2010-2013, Jordan Cristiano  
<jordan DOT first six letters of last name AT gmail DOT com>

Redistribution and use of the SizzlingPlugins code or any derivative
works are permitted provided that the following conditions are met:

* _Redistributions may not be sold, nor may they be used in a commercial
product or activity, without specific prior written permission from
Jordan Cristiano._
 
* Redistributions that are modified from the original source must include
the complete source code, including the source code for all components
used by binaries built from the modified sources.

* Redistributions must reproduce the above copyright notice, this list
of conditions, and the following disclaimer in the documentation and/or
other materials provided with the distribution.

THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

- - -
All individuals associated with SizzlingPlugins do not claim ownership of 
the contents of the directory "/external" and/or the directory "/lib",
which do not fall under the previous license.

SizzlingPlugins includes code Copyright (c) Valve Corporation and is licensed separately.

SizzlingPlugins includes code Copyright (c) Daniel Stenberg, under [the curl license](http://curl.haxx.se/docs/copyright.html).

SizzlingPlugins includes code Copyright (c) Google Inc., under the [new BSD license](http://opensource.org/licenses/BSD-3-Clause).

SizzlingPlugins includes code Copyright (c) Offspark B.V. and is distributed under the [GNU Public License Version 2.0 (GPL v2.0)](http://www.gnu.org/licenses/old-licenses/gpl-2.0.html).

Source and Team Fortress are trademarks and/or registered trademarks of Valve Corporation.
