.. Auto-generated by help-rst from "mirtk copy-pointset-attributes -h" output


Copies point and/or cell data from a source point set to a target point set
and writes the resulting amended point set to the specified output file.
When no separate output file is specified, the target point set is overwritten.

If the point sets have differing number of points or cells, respectively,
zero entries are either added to the target arrays or only the first n tuples
of the source arrays are copied. In case of the -points option, the remaining
target points are kept unchanged.

If the <attr> argument is given to any of the option below, the copied data
array is set as the new "scalars", "vectors", "tcoords", or "normals"
attribute, respectively, of the output point set.

When no options are given, the :option:`copy-pointset-attributes -scalars` are copied over.
