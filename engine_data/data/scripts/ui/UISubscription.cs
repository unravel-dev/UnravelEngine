using System;

namespace Unravel.Core
{
    /// <summary>
    /// A UI event subscription made with <see cref="UIElement.On{T}(string, Action{T}, EventPhase)"/> or
    /// <see cref="UIElement.Once{T}(string, Action{T})"/>. Dispose it to unsubscribe, which also works
    /// for lambdas. It also ends by itself when its element is destroyed or its script is destroyed.
    /// </summary>
    public sealed class UISubscription : IDisposable
    {
        internal static readonly UISubscription Inactive = new UISubscription(null);

        private readonly UIEventManager.Subscription subscription;

        internal UISubscription(UIEventManager.Subscription subscription)
        {
            this.subscription = subscription;
        }

        /// <summary>
        /// Whether the callback still runs for new events.
        /// </summary>
        public bool isActive => subscription != null && !subscription.removed;

        /// <summary>
        /// Unsubscribes; does nothing when the subscription already ended.
        /// </summary>
        public void Dispose()
        {
            UIEventManager.Cancel(subscription);
        }
    }
}
