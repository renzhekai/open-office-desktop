'use strict';
const path = require('path');

module.exports = function (grunt) {
  const buildRoot = path.resolve(__dirname, '../build/full-source/DesktopEditors/web-apps/build');
  if (path.resolve(process.cwd()) !== buildRoot) {
    throw new Error('Run only from the isolated web-apps/build directory');
  }
  require(path.join(buildRoot, 'Gruntfile.js'))(grunt);
  // Keep every raster resource without fetching or executing image compressor binaries.
  // SVG processing and the upstream JavaScript/CSS build remain enabled.
  grunt.registerMultiTask('imagemin', 'Copy original raster assets without recompression', function () {
    let copied = 0;
    this.files.forEach(file => {
      const inputs = file.src.filter(source => grunt.file.isFile(source));
      if (!inputs.length) return;
      if (inputs.length !== 1) grunt.fail.fatal('Unexpected image mapping: ' + JSON.stringify(file));
      grunt.file.copy(inputs[0], file.dest);
      copied++;
    });
    grunt.log.ok('Copied ' + copied + ' original raster images; recompression disabled.');
  });
};
