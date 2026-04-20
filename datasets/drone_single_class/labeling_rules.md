# Labeling Rules

Scope:
- this dataset is for single-class `drone` detection
- only visible drone bodies should be labeled

Label as `drone` when:
- the object is an unmanned aerial vehicle visible in the frame
- the drone is small, but still visually distinguishable from noise
- the drone is partially occluded but the object identity is still reasonably clear

Do not label as `drone` when:
- the object is only a few uncertain pixels with no reliable identity
- the object is a bird, airplane, helicopter, kite, balloon, insect, or glare
- the object is fully hidden or indistinguishable

Box rules:
- draw one tight box around the visible drone body
- do not include large empty background margins
- for partially occluded drones, box only the visible extent
- if two drones are visible, use two separate boxes

Difficult cases:
- very tiny but identifiable drone: label it
- very tiny and ambiguous dot: skip it
- motion blur: label only if still confidently a drone
- strong backlight or silhouette: label only if identity is still reliable

Negative sample policy:
- keep frames with birds, planes, kites, balloons, and empty sky
- do not create fake labels for negatives
- negatives are still valuable through unlabeled images in training workflows or as review material

Quality bar:
- consistency is more important than squeezing every borderline object into the dataset
- when uncertain, mark the sample for review instead of forcing a label
