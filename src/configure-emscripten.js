Module['locateFile'] = function(path, prefix) {
  if (path.endsWith("finvenkisto.data")) {
    return prefix
    + path.substring(0, path.length - 16)
    + "data/finvenkisto.data";
  } else {
    return prefix + path;
  }
}
