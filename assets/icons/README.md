# Icons

The 16x16 and 32x32 icon set the examples draw from, one `.ico` per icon,
named by number. `index.png` is a contact sheet of all of them with those
numbers under each, which is the quickest way to find the one you want.

Load one with `LoadImageA` and put it in an image list:

```c
HICON icon = (HICON)LoadImageA(NULL, "assets/icons/4.ico", IMAGE_ICON,
                               16, 16, LR_LOADFROMFILE);
ImageList_AddIcon(images, icon);
DestroyIcon(icon);
```

A `.ico` holds several sizes; `LoadImageA` takes whichever is nearest the one
asked for. The mask comes with it, so nothing needs a colour nominated as
transparent.
