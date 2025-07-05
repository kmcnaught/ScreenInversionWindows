

Task 1
Read and understand the code. Is any of it redundant or can it be simplified? If so, please refactor in small and self-contained commits. If you think you've found any bugs, stop and let me know. 

Task 2
The geometry of the inverted region is strange because of the taken up by the title bar. When I use the two click method to select a region, I end up with part of the content cut off because the window position defined by my click is at the top of the title bar, but the actual inverted region begins at the bottom of the title bar. Can you add appropriate offsets to account for the size of the appbar and the window frame so that the clicked rectangle defines the INSIDE of the window, not the outer border? After making this commit, stop so I can test it thoroughly.

Task 3
Currently you can press 1-9 to load an existing saved region. When you do this, the inversion state doesn't seem to be correctly loaded / applied, it always ends up inverted. Please fix this. 

Task 4
Currently you can only load a saved region at start up. make it so that you can use the shortcut to load 1-9 at any point. Make sure you check the state management carefully so that it's definitely ending up in the same state it would have done if you'd loaded it from the start. 


