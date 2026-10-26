import CoreGraphics

// Where the card goes: under the anchor (the selection or the cursor), above it when there is no room, inside the screen.
public enum Placement {
    public static let gap: CGFloat = 6

    // Cocoa coordinates (origin bottom-left); anchor is the selection's rect or a zero-size rect at the cursor.
    public static func frame(size: CGSize, anchor: CGRect, screen: CGRect) -> CGRect {
        var x = anchor.minX
        var y = anchor.minY - gap - size.height
        if y < screen.minY { y = anchor.maxY + gap }
        x = min(max(x, screen.minX), screen.maxX - size.width)
        y = min(max(y, screen.minY), screen.maxY - size.height)
        return CGRect(origin: CGPoint(x: x, y: y), size: size)
    }

    // The same top edge after the card's height changed, kept inside the screen.
    public static func resized(_ frame: CGRect, height: CGFloat, screen: CGRect) -> CGRect {
        var f = frame
        f.origin.y = frame.maxY - height
        f.size.height = height
        if f.minY < screen.minY { f.origin.y = screen.minY }
        return f
    }
}
