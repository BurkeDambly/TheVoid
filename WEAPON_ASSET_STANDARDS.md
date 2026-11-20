# Cold Loot weapon asset standards

Use the fitted airsoft prop as the scale reference for future Cold Loot weapons. The reference prop is **175 cm** long in game. Weapon length can vary with the design. Blender scenes use 1 unit per meter, so 0.01 Blender units equals 1 Unreal centimeter at the intended import scale.

| Feature | In-game standard |
| --- | --- |
| Width at the grip and main receiver | Aim for **10.8 cm**. Keep the core body between **9.6 and 12 cm** wide at the flipper contact area. Measure side to side along local Y. |
| Width including exterior detail | The fitted reference reaches **12.3 cm** across its body detail. Check any wider controls, trim, stocks, and magazines in the penguin pose. |
| Picatinny-style optic rail | Use a **7.71 cm** visual width across the top rail. Match the profile and lug spacing in the reference asset. Rail length can vary with the weapon. |
| Weapon and optic axes | Use **+X forward**, **+Z up**, with applied scale at **1, 1, 1**. |

## Weapon contact

Every weapon needs an empty named `Weapon_Contact` parented to its body. Put the empty directly on the flipper-facing side of the grip or receiver. The reference uses the flat receiver panel behind the drum, beside the trigger opening. Align the empty to the weapon with **+X forward**, **+Z up**, and **+Y into the weapon**.

The character uses one shared `Flipper_Weapon_Contact` parented to the `Flipper.R` bone. Place it around the middle of the inside flipper surface, toward the tip. In the reference pose, the flipper is raised 110° from its resting position and turned outward 14° for stock clearance. Align the contact’s **+X** with the penguin’s forward direction, **+Z** with up, and **+Y** inward from the flipper toward the weapon.

Snap the two contact transforms together by matching both position and rotation. If attaching the weapon body in code, set its world transform to the flipper contact’s world transform multiplied by the inverse of the weapon contact’s body-local transform. The weapon origin can then stay wherever the asset needs it. Keep this character contact fixed when swapping weapons and adjust the weapon-side contact for each design.

In the reference Blender files, select `Weapon_Contact` and edit **Contact X**, **Contact Y**, and **Contact Z** under **Object Properties → Custom Properties**. These values move the marker in weapon-local space. In the penguin fit scene, the weapon moves automatically to keep the two points snapped. To place the shared mount elsewhere on the character, move `Flipper_Weapon_Contact` in Object Mode. Copy the final weapon-side values into the standalone weapon file when you want to reuse the tuned placement.

## Interchangeable optics

Place a body-parented empty named `Optic_Mount` on every compatible weapon. Center it across the rail at the top seating plane and align its axes to the weapon. On the reference prop, the empty sits over the center slot.

Set each optic’s origin at the center of the underside of its rail base, with the same forward and up axes. In Blender, align that origin and orientation to `Optic_Mount`. In Unreal, reproduce that transform as a weapon socket and attach the optic with zero relative location and rotation and unit relative scale. Keep the rail profile consistent so the same optic base looks seated when swapped between weapons.

## Fit and animation checks

Use the penguin’s raised right-flipper pose to check new weapons. Verify that the two contacts coincide, the weapon points straight ahead, and the grip meets the inside flipper surface. A little mesh overlap at the hold is fine. Check the stock against the torso and test slide travel and magazine removal independently. Keep the slide and magazine as separate meshes with their animation pivots in place.

- [Reference weapon and optic mount](<SourceAssets/Weapons/Blender/Airsoft Prop.blend>)
- [Penguin weapon fit scene](<SourceAssets/Weapons/Blender/Cold Loot - Penguin Weapon Fit.blend>)
